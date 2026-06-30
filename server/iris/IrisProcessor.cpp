#include "IrisProcessor.h"
using namespace std;

//אחראי על שכבת האלגוריתם בלבד: חילוץ קוד ביומטרי והשוואת תבניות. אינו מכיר את DatabaseManager ואינו מבצע שאילתות DB.
IrisProcessor::IrisProcessor(int normWidth, int normHeight, double matchThreshold)
    : m_extractor(normWidth, normHeight)
    , m_matchThreshold(matchThreshold)
{}

IrisCode IrisProcessor::extractCode(const vector<uint8_t>& imageData) const
{
    return m_extractor.extract(imageData);
}

double IrisProcessor::compare(const IrisCode& probe, const IrisCode& gallery) const
{
    return m_matcher.compare(probe, gallery);
}

