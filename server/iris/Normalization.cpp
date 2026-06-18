#include "Normalization.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <vector>
using namespace std;
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


Normalization::Normalization(int outWidth, int outHeight)
    : m_width(outWidth), m_height(outHeight)
{}


CV_EXPORTS cv::Mat Normalization::normalize(const cv::Mat& srcGray,
                                             const IrisRegion& region,
                                             cv::Mat& occlusionMask) const
{
    cv::Mat normalized(m_height, m_width, CV_8UC1, cv::Scalar(0));
    occlusionMask = cv::Mat(m_height, m_width, CV_8UC1, cv::Scalar(255));
    const float fW = float(srcGray.cols - 1), fH = float(srcGray.rows - 1);
    for (int row = 0; row < m_height; ++row) {
        float rNorm = (float(row) + 0.5f) / float(m_height);
        float cx = region.pupilCenter.x * (1.f - rNorm) + region.irisCenter.x * rNorm;
        float cy = region.pupilCenter.y * (1.f - rNorm) + region.irisCenter.y * rNorm;
        float R  = region.pupilRadius   * (1.f - rNorm) + region.irisRadius   * rNorm;
        for (int col = 0; col < m_width; ++col) {
            double theta = 2.0 * M_PI * col / m_width;
            float  srcX  = cx + R * float(cos(theta));
            float  srcY  = cy + R * float(sin(theta));
            if (srcX < 0.f || srcX > fW || srcY < 0.f || srcY > fH) {
                occlusionMask.at<uint8_t>(row, col) = 0;
                continue;
            }
            int x0 = int(srcX), y0 = int(srcY);
            int x1 = min(x0 + 1, (int)fW);
            int y1 = min(y0 + 1, (int)fH);
            float fx = srcX - x0, fy = srcY - y0;
            float v = (1-fx)*(1-fy)*srcGray.at<uint8_t>(y0, x0)
                    + fx    *(1-fy)*srcGray.at<uint8_t>(y0, x1)
                    + (1-fx)* fy   *srcGray.at<uint8_t>(y1, x0)
                    + fx    * fy   *srcGray.at<uint8_t>(y1, x1);
            normalized.at<uint8_t>(row, col) = uint8_t(v + 0.5f);
        }
    }
    cv::Mat brightMask;
    cv::threshold(normalized, brightMask, 240, 255, cv::THRESH_BINARY);
    cv::bitwise_and(occlusionMask, ~brightMask, occlusionMask);
    cv::Mat darkMask;
    cv::threshold(normalized, darkMask, 35, 255, cv::THRESH_BINARY_INV);
    cv::bitwise_and(occlusionMask, ~darkMask, occlusionMask);
    {
       vector<uint8_t> rowBuf(m_width);
       vector<uint8_t> absDev(m_width);
        for (int r = 0; r < m_height; ++r) {
            const uint8_t* rowPtr = normalized.ptr<uint8_t>(r);
            uint8_t* maskPtr      = occlusionMask.ptr<uint8_t>(r);
            for (int c = 0; c < m_width; ++c) rowBuf[c] = rowPtr[c];
            std::nth_element(rowBuf.begin(),
                             rowBuf.begin() + m_width / 2,
                             rowBuf.end());
            uint8_t med = rowBuf[m_width / 2];
            for (int c = 0; c < m_width; ++c)
                absDev[c] = uint8_t(std::abs(int(rowPtr[c]) - int(med)));
            std::nth_element(absDev.begin(),
                             absDev.begin() + m_width / 2,
                             absDev.end());
            int mad = absDev[m_width / 2];
            int thr = max(15, int(2.5 * max(mad, 6)));

            for (int c = 0; c < m_width; ++c) {
                int dev = int(rowPtr[c]) - int(med);
                if (dev > thr) maskPtr[c] = 0;
            }
        }
    }
    return normalized;
}


 // זיהוי ובידוד קשתית ואישון העין (Segmentation).
 // הפונקציה מוצאת את המיקום המדויק (מרכז ורדיוס) של האישון ושל הקשתית
 // מתוך תמונת עין בגווני אפור, על פי עקרון "המחוגה הדיגיטלית" של דאגמן (Daugman's IDO).
 // grayEye -  תמונת המקור של העין בגווני אפור (cv::Mat).
 // outRegion -מבנה נתונים שיעודכן במרכזים וברדיוסים שנמצאו.
 // return true- אם הזיהוי הושלם בהצלחה, false אחרת.
bool segmentIris(const cv::Mat& grayEye, IrisRegion& outRegion)
{
    const int W = grayEye.cols, H = grayEye.rows;
    cv::Mat blurred;
    // טשטוש התמונה כדי להפחית רעש ולהקל על זיהוי המעגלים
    cv::GaussianBlur(grayEye, blurred, cv::Size(11, 11), 3.0);
    // פונקציה פנימית לחישוב ממוצע עגול של ערכי הבהירות לאורך מעגל נתון
    auto circMean = [&](float cx, float cy, float r) -> double {
        int nS = max(32, int(2.0 * M_PI * r));
        nS = min(nS, 200);
        double sum = 0.0;
        double dTh = 2.0 * M_PI / nS;
        for (int i = 0; i < nS; ++i) {
            double th = i * dTh;
            int xi = int(std::round(cx + r * std::cos(th)));
            int yi = int(std::round(cy + r * std::sin(th)));
            if (xi >= 0 && xi < W && yi >= 0 && yi < H)
                sum += blurred.at<uint8_t>(yi, xi);
        }
        return sum / nS;
    };
    static const double GW5[5] = {1, 4, 6, 4, 1}; 
    auto ido3D = [&](int gx0, int gy0, int gx1, int gy1, int gStep,
                     int rMin, int rMax, int rStep,
                     cv::Point2f& outCtr, float& outR)
    { 
        outCtr = { float(W) / 2.f, float(H) / 2.f };
        outR   = float(rMin + rMax) / 2.f;
        double bestScore = -1e9;
        auto evalCtr = [&](float cx, float cy, int rlo, int rhi, int rs) {
            int nR = (rhi - rlo) / rs + 1;
            if (nR < 3) return;
            vector<double> m(nR);
            for (int i = 0; i < nR; ++i) {
                double v = circMean(cx, cy, float(rlo + i * rs));
                m[i] = v; 
            }
            vector<double> sm(nR);
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
            for (int i = 1; i < nR; ++i) {
                double dv = sm[i] - sm[i - 1];
                if (dv > bestScore) {
                    bestScore = dv;
                    outCtr = { cx, cy };
                    outR   = float(rlo) + (float(i) - 0.5f) * float(rs);
                }
            }
        };
        for (int cy = gy0; cy <= gy1; cy += gStep)
            for (int cx = gx0; cx <= gx1; cx += gStep)
                evalCtr(float(cx), float(cy), rMin, rMax, rStep);
        cv::Point2f cc = outCtr; float cr = outR;
        int fRMin = max(rMin, int(cr) - 6);
        int fRMax = min(rMax, int(cr) + 6);
        for (int dy = -8; dy <= 8; ++dy)
            for (int dx = -8; dx <= 8; ++dx)
                evalCtr(cc.x + float(dx), cc.y + float(dy), fRMin, fRMax, 1);
    };
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
    const int pRMin = 20;
    const int pRMax = int(float(std::min(W, H)) / 4.5f); 
    float pupilR;
    {
        int nR = pRMax - pRMin + 1;
        vector<double> m(nR), sm(nR);
        for (int i = 0; i < nR; ++i)
            m[i] = circMean(pupilCtr.x, pupilCtr.y, float(pRMin + i));
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
    const int iRMin = max(int(pupilR * 1.8f), int(float(min(W, H)) * 0.25f));
    const int iRMax = int(float(min(W, H)) * 0.46f);
    float irisR;
    {
        int nR = iRMax - iRMin + 1;
        if (nR < 3) nR = 3;
        vector<double> m(nR), sm(nR);
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
    cv::Point2f irisCtr = pupilCtr;
    outRegion.pupilCenter = pupilCtr;
    outRegion.pupilRadius = pupilR;
    outRegion.irisCenter  = irisCtr;
    outRegion.irisRadius  = irisR;
    fprintf(stderr,
        "[IDO] pupil=(%.1f,%.1f) r=%.1f  iris=(%.1f,%.1f) r=%.1f  img=%dx%d\n",
        pupilCtr.x, pupilCtr.y, pupilR,
        irisCtr.x, irisCtr.y, irisR, W, H);
    return true;
}

