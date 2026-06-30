#pragma once
#include <vector>
#include <cstdint>
#include <array>
#include <opencv2/opencv.hpp>
#include "../models/IrisCode.h"
#include "Normalization.h"
#include "LogGabor.h"

// ─── קבועי אלגוריתם חילוץ מאפיינים 
// סף לזיהוי והסרת השתקפויות בתמונה הגולמית
static constexpr int  REFLECTION_THRESHOLD = 235;
// פרמטרי CLAHE (שיפור ניגודיות מסתגל מקומי)
static constexpr double CLAHE_CLIP_LIMIT = 2.0;
static constexpr int CLAHE_TILE_W = 16;
static constexpr int CLAHE_TILE_H = 8;
// מספר רצועות אופקיות לחלוקת תמונת הנורמליזציה לפני Log-Gabor
static constexpr int   NUM_GABOR_BANDS = 4;
// תדרים מרכזיים של מסנני Log-Gabor (יחידות נורמליות, 0–0.5)
constexpr std::array<float, NUM_GABOR_BANDS> GABOR_FREQS = { 0.010f, 0.020f, 0.030f, 0.042f };
// רוחב פס לוגריתמי (Log-Gabor bandwidth)
static constexpr float GABOR_BANDWIDTH = 0.85f;


// שכבת אלגוריתם בלבד: חילוץ IrisCode מתמונה גולמית
// אינה מכירה את DatabaseManager ואינה מבצעת שאילתות DB.
class FeatureExtractor {
public:
    FeatureExtractor(int normWidth, int normHeight);

    IrisCode extract(const std::vector<uint8_t>& imageData) const;

private:
    Normalization m_norm;
    int m_normWidth;
    int m_normHeight;
};
