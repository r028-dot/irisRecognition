#pragma once
#include "../models/IrisCode.h"

// Compares two IrisCodes using masked Hamming distance
class IrisMatcher {
public:
    // Returns distance in [0.0, 1.0]. Values <= 0.32 are considered a match.
    double compare(const IrisCode& probe, const IrisCode& gallery) const {
        return probe.hammingDistance(gallery);
    }
};
