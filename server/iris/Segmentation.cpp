#include "Segmentation.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <vector>
using namespace std;
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
// סגמנטציה של הקשתית: זיהוי המרכז והרדיוס של האישון והקשתית בתמונה של העין באמצעות סריקה תלת-ממדית (IDO) על בסיס ממוצע עגול של ערכי הבהירות לאורך מעגלים שונים.
// הפונקציה מחזירה true אם הסגמנטציה הצליחה, ו-false אם לא.
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
    // פונקציה פנימית לביצוע סריקה תלת-ממדית (IDO) למציאת המרכז והרדיוס הטובים ביותר של המעגלים (אישון וקשתית)
    auto ido3D = [&](int sx, int sy, int ex, int ey, int gStep,
                     int rMin, int rMax, int rStep,
                     cv::Point2f& outCtr, float& outR)
    {
        outCtr = { float(W) / 2.f, float(H) / 2.f };
        outR   = float(rMin + rMax) / 2.f;
        double bestScore = -1e9;
        // שלב א': סריקה למציאת המרכז והרדיוס הטובים ביותר של המעגלים
        auto evalCtr = [&](float cx, float cy, int rlo, int rhi, int rs)
        {
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
                    if (!(j < 0 || j >= nR))
                    {
                        s += GW5[k + 2] * m[j];
                        w += GW5[k + 2];
                    }
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
        for (int cy = sy; cy <= ey; cy += gStep)
            for (int cx = sx; cx <= ex; cx += gStep)
                evalCtr(float(cx), float(cy), rMin, rMax, rStep);
        cv::Point2f cc = outCtr; float cr = outR;
        int fRMin = max(rMin, int(cr) - 6);
        int fRMax = min(rMax, int(cr) + 6);
        for (int dy = -8; dy <= 8; ++dy)
            for (int dx = -8; dx <= 8; ++dx)
                evalCtr(cc.x + float(dx), cc.y + float(dy), fRMin, fRMax, 1);
    };
    // אתחול גס: מציאת הנקודה הכהה ביותר בתמונה המטושטשת כאומדן ראשוני למרכז האישון
    cv::Mat strongBlur;
    cv::GaussianBlur(grayEye, strongBlur, cv::Size(51, 51), 12.0);
    cv::Point2f pupilCtr;
    cv::Point2f coarsePupilCtr;
    {
        cv::Rect roi(W / 5, H / 5, W * 3 / 5, H * 3 / 5);
        cv::Mat roiStrong = strongBlur(roi);
        cv::Point minLoc;
        cv::minMaxLoc(roiStrong, nullptr, nullptr, &minLoc, nullptr);
        pupilCtr.x = float(roi.x + minLoc.x);
        pupilCtr.y = float(roi.y + minLoc.y);
        coarsePupilCtr = pupilCtr;
    }
    // שלב ב': סריקה תלת-ממדית (IDO) סביב האומדן הראשוני למציאת מרכז ורדיוס האישון המדויקים
    const int pRMin = 20;
    const int pRMax = int(float(min(W, H)) / 4.5f);
    float pupilR;
    {
        int sx = max(0,   int(pupilCtr.x) - 30);
        int ex = min(W-1, int(pupilCtr.x) + 30);
        int sy = max(0,   int(pupilCtr.y) - 30);
        int ey = min(H-1, int(pupilCtr.y) + 30);
        ido3D(sx, sy, ex, ey, 4, pRMin, pRMax, 2, pupilCtr, pupilR);
    }
    // שלב ג': סריקה תלת-ממדית (IDO) סביב האומדן הראשוני למציאת מרכז ורדיוס הקשתית המדויקים
    const int iRMin = max(int(pupilR * 1.8f), int(float(min(W, H)) * 0.25f));
    const int iRMax = int(float(min(W, H)) * 0.46f);
    cv::Point2f irisCtr;
    float irisR;
    {
        int sx = max(0, int(pupilCtr.x) - 40);
        int ex = min(W-1, int(pupilCtr.x) + 40);
        int sy = max(0, int(pupilCtr.y) - 40);
        int ey = min(H-1, int(pupilCtr.y) + 40);
        ido3D(sx, sy, ex, ey, 4, iRMin, iRMax, 2, irisCtr, irisR);
    }

    // יציב: אם מרכז האישון זז רחוק מדי מהאומדן הגס, נחזור לאומדן הגס.
    // זה מונע קפיצות סגמנטציה שמנפחות HD בין תמונות של אותו אדם.
    {
        float dx = pupilCtr.x - coarsePupilCtr.x;
        float dy = pupilCtr.y - coarsePupilCtr.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist > MAX_PUPIL_CENTER_SHIFT_PX) {
            pupilCtr = coarsePupilCtr;
            fprintf(stderr,
                "[IDO] fallback: pupil center shift too large (%.2f px), using coarse center (%.1f,%.1f)\n",
                dist, pupilCtr.x, pupilCtr.y);
        }
    }

    //  יציב: מרכז הקשתית אמור להיות קרוב למרכז האישון. אם הסטייה גדולה,
    // נצמיד את מרכז הקשתית למרכז האישון כדי להימנע מ-unwrapping לא יציב.
    {
        float dx = irisCtr.x - pupilCtr.x;
        float dy = irisCtr.y - pupilCtr.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist > MAX_IRIS_CENTER_OFFSET_PX) {
            fprintf(stderr,
                "[IDO] fallback: iris center offset too large (%.2f px), forcing iris center to pupil center\n",
                dist);
            irisCtr = pupilCtr;
        }
    }
    // בדיקת תקינות: רדיוסים חייבים להיות חיוביים ובגבולות הגיוניים
    if (pupilR <= 0.f || irisR <= 0.f) {
        fprintf(stderr, "[IDO] FAIL: non-positive radius (pupil=%.1f iris=%.1f)\n", pupilR, irisR);
        return false;
    }
    // בדיקה: האישון חייב להיות קטן מהקשתית עם מרווח סביר
    if (irisR <= pupilR * 1.2f) {
        fprintf(stderr, "[IDO] FAIL: iris radius (%.1f) not significantly larger than pupil (%.1f)\n", irisR, pupilR);
        return false;
    }
    // בדיקה: המרכזים חייבים להיות בתוך גבולות התמונה
    if (pupilCtr.x < 0.f || pupilCtr.x >= float(W) ||
        pupilCtr.y < 0.f || pupilCtr.y >= float(H) ||
        irisCtr.x  < 0.f || irisCtr.x  >= float(W) ||
        irisCtr.y  < 0.f || irisCtr.y  >= float(H)) {
        fprintf(stderr, "[IDO] FAIL: center out of image bounds\n");
        return false;
    }
    // עדכון מבנה הנתונים עם התוצאות שנמצאו
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
