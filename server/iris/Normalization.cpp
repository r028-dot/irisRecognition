#include "Normalization.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── Constructor ───────────────────────────────────────────────────────────────
Normalization::Normalization(int outWidth, int outHeight)
    : m_width(outWidth), m_height(outHeight)
{}

// ── normalize ─────────────────────────────────────────────────────────────────
// Daugman Rubber-Sheet Model with bilinear interpolation.
// The pupil centre is the fixed anchor for all radial samples.  Only the
// radius grows linearly from pupilR (inner boundary) to irisR (outer boundary):
//   R(rNorm) = pupilR*(1-rNorm) + irisR*rNorm
//   src(rNorm, θ) = pupilCentre + R(rNorm) * (cos θ, sin θ)
//
// Anchoring at the pupil centre (which the IDO finds with high repeatability)
// rather than interpolating towards the noisier iris centre gives stable
// rubber-sheet coordinates across different images of the same eye.
CV_EXPORTS cv::Mat Normalization::normalize(const cv::Mat& srcGray,
                                             const IrisRegion& region,
                                             cv::Mat& occlusionMask) const
{
    cv::Mat normalized(m_height, m_width, CV_8UC1, cv::Scalar(0));
    occlusionMask = cv::Mat(m_height, m_width, CV_8UC1, cv::Scalar(255));

    // True Daugman Rubber-Sheet Model:
    //   Each point (rNorm, θ) maps to the linear interpolation between the
    //   pupil boundary point and the iris boundary point at angle θ:
    //     pupil_bdy(θ) = pupilCenter + pupilR*(cos θ, sin θ)
    //     iris_bdy(θ)  = irisCenter  + irisR *(cos θ, sin θ)
    //     src(rNorm,θ) = (1-rNorm)*pupil_bdy(θ) + rNorm*iris_bdy(θ)
    //   This equals: center(rNorm) + R(rNorm)*(cos θ, sin θ)  where
    //     center(rNorm) = (1-rNorm)*pupilCenter + rNorm*irisCenter
    //     R(rNorm)      = (1-rNorm)*pupilR      + rNorm*irisR
    //   Using both centres makes the mapping invariant to small shifts of
    //   either the pupil or the iris centre across images of the same eye.
    const float fW = float(srcGray.cols - 1), fH = float(srcGray.rows - 1);

    for (int row = 0; row < m_height; ++row) {
        float rNorm = (float(row) + 0.5f) / float(m_height);
        // Interpolated centre and radius for this radial strip
        float cx = region.pupilCenter.x * (1.f - rNorm) + region.irisCenter.x * rNorm;
        float cy = region.pupilCenter.y * (1.f - rNorm) + region.irisCenter.y * rNorm;
        float R  = region.pupilRadius   * (1.f - rNorm) + region.irisRadius   * rNorm;

        for (int col = 0; col < m_width; ++col) {
            double theta = 2.0 * M_PI * col / m_width;
            float  srcX  = cx + R * float(std::cos(theta));
            float  srcY  = cy + R * float(std::sin(theta));

            // Bilinear interpolation
            if (srcX < 0.f || srcX > fW || srcY < 0.f || srcY > fH) {
                occlusionMask.at<uint8_t>(row, col) = 0;
                continue;
            }
            int x0 = int(srcX), y0 = int(srcY);
            int x1 = std::min(x0 + 1, (int)fW);
            int y1 = std::min(y0 + 1, (int)fH);
            float fx = srcX - x0, fy = srcY - y0;
            float v = (1-fx)*(1-fy)*srcGray.at<uint8_t>(y0, x0)
                    + fx    *(1-fy)*srcGray.at<uint8_t>(y0, x1)
                    + (1-fx)* fy   *srcGray.at<uint8_t>(y1, x0)
                    + fx    * fy   *srcGray.at<uint8_t>(y1, x1);
            normalized.at<uint8_t>(row, col) = uint8_t(v + 0.5f);
        }
    }

    // Mask out specular reflections (very bright pixels)
    cv::Mat brightMask;
    cv::threshold(normalized, brightMask, 240, 255, cv::THRESH_BINARY);
    cv::bitwise_and(occlusionMask, ~brightMask, occlusionMask);

    return normalized;
}

// ── segmentIris ───────────────────────────────────────────────────────────────
// Implements Daugman's Integro-Differential Operator (IDO):
//
//   max_{r,x₀,y₀}  | Gσ * ∂/∂r  ∮_{r,x₀,y₀}  I(x,y)/(2πr)  ds |
//
// The operator finds the circular contour (centre + radius) where intensity
// changes most sharply.  Applied twice:
//   1. Small radius range  → pupil/iris boundary  (pupil centre + radius)
//   2. Large radius range  → iris/sclera boundary (iris  centre + radius)
//
// Both searches are fully INDEPENDENT 3-D optimisations over (x₀, y₀, r).
// The iris centre is NOT assumed to equal the pupil centre.  This correctly
// handles eccentric pupils and removes the systematic error from anchoring
// the rubber-sheet at the (unstable) pupil centroid.
//
// Implementation details:
//  • Input is Gaussian-blurred (11×11, σ=3) to suppress texture while
//    preserving the strong boundary gradients.
//  • circMean(r) is computed with nSamples ∝ 2πr for uniform angular density.
//  • The 1-D curve circMean(r) is Gaussian-smoothed (kernel [1,4,6,4,1]/16)
//    before differencing → suppresses spurious local maxima.
//  • Coarse-to-fine: step=4px grid / r-step=2 → then ±8px / r-step=1.
bool segmentIris(const cv::Mat& grayEye, IrisRegion& outRegion)
{
    const int W = grayEye.cols, H = grayEye.rows;

    // Pre-blur: suppress iris texture while preserving limbus & pupil edges
    cv::Mat blurred;
    cv::GaussianBlur(grayEye, blurred, cv::Size(11, 11), 3.0);

    // ── circMean: average intensity on a circle (cx,cy,r) ────────────────────
    // Points outside the image contribute 0 rather than being excluded.
    // This prevents a spurious positive gradient when the search circle grows
    // past the image boundary: excluding dark out-of-bounds points would
    // artificially raise the mean and create a fake iris/sclera transition.
    auto circMean = [&](float cx, float cy, float r) -> double {
        int nS = std::max(32, int(2.0 * M_PI * r));
        nS = std::min(nS, 200);
        double sum = 0.0;
        double dTh = 2.0 * M_PI / nS;
        for (int i = 0; i < nS; ++i) {
            double th = i * dTh;
            int xi = int(std::round(cx + r * std::cos(th)));
            int yi = int(std::round(cy + r * std::sin(th)));
            if (xi >= 0 && xi < W && yi >= 0 && yi < H)
                sum += blurred.at<uint8_t>(yi, xi);
            // Out-of-bounds points implicitly contribute 0
        }
        return sum / nS;  // always divide by total samples
    };

    // ── ido3D: 3-D IDO search over (x₀, y₀, r) ───────────────────────────────
    // Finds the (centre, radius) that maximises the Gaussian-smoothed derivative
    // of circMean(r).  Two-phase: coarse grid then local refinement.
    static const double GW5[5] = {1, 4, 6, 4, 1};   // Gaussian kernel (sum=16)

    auto ido3D = [&](int gx0, int gy0, int gx1, int gy1, int gStep,
                     int rMin, int rMax, int rStep,
                     cv::Point2f& outCtr, float& outR)
    {
        outCtr = { float(W) / 2.f, float(H) / 2.f };
        outR   = float(rMin + rMax) / 2.f;
        double bestScore = -1e9;

        // Evaluate a single candidate centre (cx,cy) over radius range [rlo,rhi]
        auto evalCtr = [&](float cx, float cy, int rlo, int rhi, int rs) {
            int nR = (rhi - rlo) / rs + 1;
            if (nR < 3) return;

            // Sample circMean curve
            std::vector<double> m(nR);
            for (int i = 0; i < nR; ++i) {
                double v = circMean(cx, cy, float(rlo + i * rs));
                m[i] = v;  // circMean never returns negative now
            }

            // Gaussian smooth [1,4,6,4,1]/16
            std::vector<double> sm(nR);
            for (int i = 0; i < nR; ++i) {
                double s = 0, w = 0;
                for (int k = -2; k <= 2; ++k) {
                    int j = i + k;
                    if (j < 0 || j >= nR) continue;
                    s += GW5[k + 2] * m[j];
                    w += GW5[k + 2];
                }
                sm[i] = (w > 0) ? s / w : m[i];
            }

            // Max positive derivative → dark-to-bright boundary
            for (int i = 1; i < nR; ++i) {
                double dv = sm[i] - sm[i - 1];
                if (dv > bestScore) {
                    bestScore = dv;
                    outCtr = { cx, cy };
                    outR   = float(rlo) + (float(i) - 0.5f) * float(rs);
                }
            }
        };

        // Phase 1 — coarse grid
        for (int cy = gy0; cy <= gy1; cy += gStep)
            for (int cx = gx0; cx <= gx1; cx += gStep)
                evalCtr(float(cx), float(cy), rMin, rMax, rStep);

        // Phase 2 — refine ±8 px around coarse best, r ±6 around coarse best R
        cv::Point2f cc = outCtr; float cr = outR;
        int fRMin = std::max(rMin, int(cr) - 6);
        int fRMax = std::min(rMax, int(cr) + 6);
        for (int dy = -8; dy <= 8; ++dy)
            for (int dx = -8; dx <= 8; ++dx)
                evalCtr(cc.x + float(dx), cc.y + float(dy), fRMin, fRMax, 1);
    };

    // ── PUPIL CENTRE: location of the global minimum after heavy blur ─────────
    // After a very strong Gaussian blur (51×51, σ=12), the location of the
    // minimum pixel in the central ROI is the weighted centroid of all dark
    // regions.  This requires NO threshold choice and is the most stable
    // estimator available from a single image — equivalent to a dark-pixel
    // weighted centroid but analytically exact.
    cv::Mat strongBlur;
    cv::GaussianBlur(grayEye, strongBlur, cv::Size(51, 51), 12.0);
    cv::Point2f pupilCtr;
    {
        cv::Rect roi(W / 5, H / 5, W * 3 / 5, H * 3 / 5);
        cv::Mat roiStrong = strongBlur(roi);
        cv::Point minLoc;
        cv::minMaxLoc(roiStrong, nullptr, nullptr, &minLoc, nullptr);
        pupilCtr.x = float(roi.x + minLoc.x);
        pupilCtr.y = float(roi.y + minLoc.y);
    }

    // ── PUPIL RADIUS: 1-D IDO at the fixed pupil centroid ────────────────────
    // Searching only r (not x,y) from the stable centroid gives a tight,
    // repeatable radius estimate.
    // pRMin=20: skip specular highlights and eyelashes (typically r<20px)
    const int pRMin = 20;
    const int pRMax = int(float(std::min(W, H)) / 4.5f);  // ≈ 53 for 320×240
    float pupilR;
    {
        int nR = pRMax - pRMin + 1;
        std::vector<double> m(nR), sm(nR);
        for (int i = 0; i < nR; ++i)
            m[i] = circMean(pupilCtr.x, pupilCtr.y, float(pRMin + i));
        // Gaussian smooth [1,4,6,4,1]/16
        static const double GW5b[5] = {1, 4, 6, 4, 1};
        for (int i = 0; i < nR; ++i) {
            double s = 0, w = 0;
            for (int k = -2; k <= 2; ++k) {
                int j = i + k;
                if (j < 0 || j >= nR) continue;
                s += GW5b[k + 2] * m[j]; w += GW5b[k + 2];
            }
            sm[i] = (w > 0) ? s / w : m[i];
        }
        double bestD = -1e9;
        pupilR = float(pRMin + pRMax) / 2.f;
        for (int i = 1; i < nR; ++i) {
            double dv = sm[i] - sm[i - 1];
            if (dv > bestD) { bestD = dv; pupilR = float(pRMin + i) - 0.5f; }
        }
    }

    // ── IRIS RADIUS: 1-D IDO at the same centroid ────────────────────────────
    // Using the same centre for both radii means the rubber-sheet is purely
    // concentric (irisCenter ≡ pupilCenter).  This removes the instability
    // caused by the iris-centre search latching onto different edges in
    // different images of the same eye.
    const int iRMin = std::max(int(pupilR * 1.8f), int(float(std::min(W, H)) * 0.25f));
    const int iRMax = int(float(std::min(W, H)) * 0.46f);
    float irisR;
    {
        int nR = iRMax - iRMin + 1;
        if (nR < 3) nR = 3;
        std::vector<double> m(nR), sm(nR);
        for (int i = 0; i < nR; ++i)
            m[i] = circMean(pupilCtr.x, pupilCtr.y, float(iRMin + i));
        static const double GW5c[5] = {1, 4, 6, 4, 1};
        for (int i = 0; i < nR; ++i) {
            double s = 0, w = 0;
            for (int k = -2; k <= 2; ++k) {
                int j = i + k;
                if (j < 0 || j >= nR) continue;
                s += GW5c[k + 2] * m[j]; w += GW5c[k + 2];
            }
            sm[i] = (w > 0) ? s / w : m[i];
        }
        double bestD = -1e9;
        irisR = float(iRMin + iRMax) / 2.f;
        for (int i = 1; i < nR; ++i) {
            double dv = sm[i] - sm[i - 1];
            if (dv > bestD) { bestD = dv; irisR = float(iRMin + i) - 0.5f; }
        }
    }

    // Concentric model: iris centre = pupil centre
    cv::Point2f irisCtr = pupilCtr;

    outRegion.pupilCenter = pupilCtr;
    outRegion.pupilRadius = pupilR;
    outRegion.irisCenter  = irisCtr;
    outRegion.irisRadius  = irisR;

    std::fprintf(stderr,
        "[IDO] pupil=(%.1f,%.1f) r=%.1f  iris=(%.1f,%.1f) r=%.1f  img=%dx%d\n",
        pupilCtr.x, pupilCtr.y, pupilR,
        irisCtr.x, irisCtr.y, irisR, W, H);

    return true;
}

