#pragma once
#include "../models/IrisCode.h"
#include <cstdint>

class IrisMatcher {
public:
    static constexpr int MIN_INTERSECTION_BITS = 400;
    static constexpr int MAX_SHIFT = 8;

    double compare(const IrisCode& probe, const IrisCode& gallery) const;

private:
    // היסט מחזורי של s עמדות דגימה זוויתית בתוך כל קבוצת 64 ביט.
    static IrisCode cyclicShiftPositions(const IrisCode& code, int s);
    double hammingDistance(const IrisCode& a, const IrisCode& b) const;
};
