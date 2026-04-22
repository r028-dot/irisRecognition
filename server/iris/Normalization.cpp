#include "Normalization.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── Constructor ──────────────────────────────────────────────────────────────────
Normalization::Normalization(int outWidth, int outHeight)
    : m_width(outWidth), m_height(outHeight)
{}

// ── normalize ───────────────────────────────────────────────────────────────
// Daugman Rubber Sheet Model:
//   For each output pixel (theta_col, r_row):
//     theta = 2*pi * col / W
//     r     =         row / H          (normalised radial position [0,1])
//     x_iris = pupilCenter.x + (1-r)*pupilRadius*cos(theta)
//              + r * irisRadius*cos(theta)
//           = pupilCenter.x + [pupilRadius*(1-r) + irisRadius*r] * cos(theta)
//     (similarly for y)
CV_EXPORTS cv::Mat Normalization::normalize(const cv::Mat& srcGray,
                                             const IrisRegion& region,
                                             cv::Mat& occlusionMask) const
{
    cv::Mat normalized(m_height, m_width, CV_8UC1, cv::Scalar(0));
    occlusionMask  = cv::Mat(m_height, m_width, CV_8UC1, cv::Scalar(255));

    const float px = region.pupilCenter.x;
    const float py = region.pupilCenter.y;
    const float pr = region.pupilRadius;
    const float ir = region.irisRadius;

    for (int row = 0; row < m_height; ++row) {
        float rNorm = (static_cast<float>(row) + 0.5f) / static_cast<float>(m_height);
        float R     = pr * (1.f - rNorm) + ir * rNorm;  // radius at this row

        for (int col = 0; col < m_width; ++col) {
            double theta = 2.0 * M_PI * col / m_width;

            float srcX = px + R * static_cast<float>(std::cos(theta));
            float srcY = py + R * static_cast<float>(std::sin(theta));

            int xi = static_cast<int>(std::round(srcX));
            int yi = static_cast<int>(std::round(srcY));

            if (xi < 0 || xi >= srcGray.cols ||
                yi < 0 || yi >= srcGray.rows) {
                occlusionMask.at<uint8_t>(row, col) = 0;
                continue;
            }

            normalized.at<uint8_t>(row, col) = srcGray.at<uint8_t>(yi, xi);
        }
    }

    // Remove specular reflections: very bright pixels are unreliable
    cv::Mat brightMask;
    cv::threshold(normalized, brightMask, 240, 255, cv::THRESH_BINARY);
    cv::bitwise_and(occlusionMask, ~brightMask, occlusionMask);

    return normalized;
}

// ── segmentIris ───────────────────────────────────────────────────────────────
bool segmentIris(const cv::Mat& grayEye, IrisRegion& outRegion)
{
    cv::Mat blurred;
    cv::GaussianBlur(grayEye, blurred, cv::Size(7, 7), 2.0);

    // — Pupil: darker, smaller circle —
    std::vector<cv::Vec3f> pupilCircles;
    cv::HoughCircles(blurred, pupilCircles, cv::HOUGH_GRADIENT,
                     /*dp=*/1.5, /*minDist=*/blurred.rows / 8,
                     /*param1=*/80, /*param2=*/30,
                     /*minRadius=*/10, /*maxRadius=*/blurred.cols / 5);

    if (pupilCircles.empty()) return false;

    // Pick the darkest candidate (pupil is the darkest region)
    cv::Vec3f bestPupil = pupilCircles[0];
    double    minMean   = 1e9;
    for (const auto& c : pupilCircles) {
        cv::Mat mask = cv::Mat::zeros(grayEye.size(), CV_8UC1);
        cv::circle(mask,
                   cv::Point(static_cast<int>(c[0]), static_cast<int>(c[1])),
                   static_cast<int>(c[2]), 255, -1);
        cv::Scalar m = cv::mean(grayEye, mask);
        if (m[0] < minMean) { minMean = m[0]; bestPupil = c; }
    }

    outRegion.pupilCenter = cv::Point2f(bestPupil[0], bestPupil[1]);
    outRegion.pupilRadius = bestPupil[2];

    // — Iris: larger circle, centred near pupil —
    std::vector<cv::Vec3f> irisCircles;
    int minIris = static_cast<int>(bestPupil[2] * 1.5f);
    int maxIris = static_cast<int>(std::min(grayEye.cols, grayEye.rows) / 2);
    cv::HoughCircles(blurred, irisCircles, cv::HOUGH_GRADIENT,
                     /*dp=*/1.5, /*minDist=*/blurred.rows / 4,
                     /*param1=*/60, /*param2=*/30,
                     minIris, maxIris);

    if (irisCircles.empty()) {
        // Fallback: estimate iris radius as 3x pupil
        outRegion.irisCenter = outRegion.pupilCenter;
        outRegion.irisRadius = bestPupil[2] * 3.f;
        return true;
    }

    // Choose iris circle closest to pupil center
    cv::Vec3f bestIris = irisCircles[0];
    float     minDist  = 1e9f;
    for (const auto& c : irisCircles) {
        float dx = c[0] - bestPupil[0];
        float dy = c[1] - bestPupil[1];
        float d  = std::sqrt(dx*dx + dy*dy);
        if (d < minDist) { minDist = d; bestIris = c; }
    }

    outRegion.irisCenter = cv::Point2f(bestIris[0], bestIris[1]);
    outRegion.irisRadius = bestIris[2];
    return true;
}
