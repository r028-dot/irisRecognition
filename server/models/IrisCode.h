#pragma once
#include <cstdint>
#include <vector>
#include <bitset>
#include <cstring>
#include <stdexcept>

struct IrisCode {
    uint8_t bits[256] = {};  // 2048 bits of iris texture
    uint8_t mask[256] = {};  // 1 = valid pixel, 0 = damaged/occluded

    // Masked Hamming distance [0.0, 1.0]  (lower = more similar)
    double hammingDistance(const IrisCode& other) const {
        int diffBits  = 0;
        int validBits = 0;
        for (int i = 0; i < 256; ++i) {
            uint8_t m = mask[i] & other.mask[i];
            diffBits  += static_cast<int>(std::bitset<8>((bits[i] ^ other.bits[i]) & m).count());
            validBits += static_cast<int>(std::bitset<8>(m).count());
        }
        return (validBits > 0) ? static_cast<double>(diffBits) / validBits : 1.0;
    }

    // Serialize: bits[256] || mask[256]  →  512 bytes
    std::vector<uint8_t> toBytes() const {
        std::vector<uint8_t> v(512);
        std::memcpy(v.data(),       bits, 256);
        std::memcpy(v.data() + 256, mask, 256);
        return v;
    }

    static IrisCode fromBytes(const uint8_t* data, size_t size) {
        if (size < 512) throw std::runtime_error("IrisCode::fromBytes: need 512 bytes");
        IrisCode code;
        std::memcpy(code.bits, data,       256);
        std::memcpy(code.mask, data + 256, 256);
        return code;
    }
};
