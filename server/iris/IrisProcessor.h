#pragma once
#include <vector>
#include "../models/IrisCode.h"
#include "FeatureExtractor.h"
#include "IrisMatcher.h"

// שכבת אלגוריתם בלבד: חילוץ IrisCode מתמונה גולמית והשוואה בין תבניות.
// אינה מכירה את DatabaseManager ואינה מבצעת שאילתות DB.
// ראה BiometricService לתיאום לוגיקה עסקית + DB.
class IrisProcessor {
public:
    explicit IrisProcessor(double matchThreshold = 0.32);

    // חילוץ IrisCode מתמונת עין גולמית
    IrisCode extractCode(const std::vector<uint8_t>& imageData) const;

    // השוואת שתי תבניות — מרחק Hamming ממוסך עם פיצוי סיבוב [0.0, 1.0]
    double compare(const IrisCode& probe, const IrisCode& gallery) const;

    double getThreshold() const { return m_matchThreshold; }

private:
    FeatureExtractor m_extractor;
    IrisMatcher      m_matcher;
    double           m_matchThreshold;
};
