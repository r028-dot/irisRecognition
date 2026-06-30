#pragma once
#include <opencv2/opencv.hpp>
#include "Segmentation.h"
using namespace std;

// ─── קבועי מסכת חסימה (Occlusion Mask) ──────────────────────────────────────
// פיקסלים בהירים מערך זה ייחסמו מהמסכה (השתקפות / עדשה)
static constexpr int   NORM_BRIGHT_THRESHOLD = 240;
// פיקסלים כהים מערך זה ייחסמו מהמסכה (ריסים / צל)
static constexpr int   NORM_DARK_THRESHOLD   = 35;
// מכפיל MAD לחישוב סף חריגים לכל שורה
static constexpr double MAD_MULTIPLIER       = 2.5;
// מינימום ערך MAD (מניעת סף אפסי בתמונות אחידות)
static constexpr int   MAD_MIN_MAD           = 6;
// מינימום סף חריגות מוחלט
static constexpr int   MAD_MIN_DEVIATION     = 15;
// ─────────────────────────────────────────────────────────────────────────────

// מיישם את מודל Rubber Sheet של Daugman.
// ממפה את אזור הקשתית הטבעתית לתמונה מלבנית בגודל קבוע.
class Normalization {
public:
    Normalization(int outWidth = 512, int outHeight = 64);

    cv::Mat normalize(const cv::Mat& srcGray,
                      const IrisRegion& region,
                      cv::Mat& occlusionMask) const;

private:
    int m_width;
    int m_height;
};
