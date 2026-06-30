#pragma once
#include <vector>
#include "../models/IrisCode.h"
#include "FeatureExtractor.h"
#include "IrisMatcher.h"


class IrisProcessor {
public:
    explicit IrisProcessor(int normWidth, int normHeight, double matchThreshold);

    // חילוץ IrisCode מתמונת עין גולמית
    IrisCode extractCode(const std::vector<uint8_t>& imageData) const;

    // השוואת שתי תבניות — מרחק Hamming ממוסך עם פיצוי סיבוב [0.0, 1.0]
    double compare(const IrisCode& probe, const IrisCode& gallery) const;

    double getThreshold() const { return m_matchThreshold; }

private:
    FeatureExtractor m_extractor;
    IrisMatcher m_matcher;
    double m_matchThreshold;
};
