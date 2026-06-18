#include "IrisProcessor.h"
using namespace std;
IrisProcessor::IrisProcessor(double matchThreshold)
    : m_matchThreshold(matchThreshold)
{}

IrisCode IrisProcessor::extractCode(const vector<uint8_t>& imageData) const
{
    return m_extractor.extract(imageData);
}

double IrisProcessor::compare(const IrisCode& probe, const IrisCode& gallery) const
{
    return m_matcher.compare(probe, gallery);
}

