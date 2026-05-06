#pragma once
#include "../models/IrisCode.h"
#include <algorithm>
#include <cstring>
#include <cstdint>

// Compares two IrisCodes using rotation-invariant masked Hamming distance.
//
// IrisCode layout (256 bytes = 2048 bits):
//   4 radial bands × 4 frequencies × 2 components (real, imag) = 32 filter slots
//   Each slot: 8 bytes = 64 bits = 64 angular samples spaced 5.625° apart
//   Angular position k samples the normalised strip at column k×8.
//   (512 columns / 64 samples = 8 columns per sample = 5.625°/sample)
//
// Rotation compensation: for each 8-byte (uint64) group, right-rotate by s bits.
// s angular positions = s × 5.625° of iris rotation.
// MAX_SHIFT = 8 positions = ±45° — covers normal head-tilt variation.
class IrisMatcher {
public:
    // ±8 positions × 5.625°/position = ±45° rotation search
    static constexpr int MAX_SHIFT = 8;

    // Returns minimum Hamming distance over all rotational shifts [0.0, 1.0].
    double compare(const IrisCode& probe, const IrisCode& gallery) const {
        double minDist = 1.0;
        for (int shift = -MAX_SHIFT; shift <= MAX_SHIFT; ++shift) {
            IrisCode shifted = cyclicShiftPositions(probe, shift);
            double d = shifted.hammingDistance(gallery);
            if (d < minDist) minDist = d;
        }
        return minDist;
    }

private:
    // Cyclic shift of s angular SAMPLE POSITIONS within each 64-bit filter group.
    // Positive s: feature at angular position k moves to position (k+s) mod 64.
    // Each group is stored big-endian; sample k sits at bit (63-k) of the uint64.
    // Moving k → k+s is equivalent to right-rotating the uint64 by s bits.
    static IrisCode cyclicShiftPositions(const IrisCode& code, int s) {
        if (s == 0) return code;
        IrisCode result = code;
        const int SAMPLES = 64;  // angular positions per filter component
        int sh = ((s % SAMPLES) + SAMPLES) % SAMPLES;
        if (sh == 0) return result;

        // 32 groups × 8 bytes = 256 bytes total
        for (int group = 0; group < 32; ++group) {
            int base = group * 8;

            // Load 64-bit group big-endian
            auto load64 = [&](const uint8_t* arr) -> uint64_t {
                uint64_t v = 0;
                for (int i = 0; i < 8; ++i)
                    v = (v << 8) | arr[base + i];
                return v;
            };
            auto store64 = [&](uint8_t* arr, uint64_t v) {
                for (int i = 7; i >= 0; --i) {
                    arr[base + i] = uint8_t(v & 0xFF);
                    v >>= 8;
                }
            };

            uint64_t bv = load64(result.bits);
            uint64_t mv = load64(result.mask);
            // Right-rotate by sh positions
            uint64_t bv2 = (bv >> sh) | (bv << (64 - sh));
            uint64_t mv2 = (mv >> sh) | (mv << (64 - sh));
            store64(result.bits, bv2);
            store64(result.mask, mv2);
        }
        return result;
    }
};
