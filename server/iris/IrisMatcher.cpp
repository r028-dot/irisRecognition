#include "IrisMatcher.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <bitset>
using namespace std;

//השוואת שתי תבניות — מרחק Hamming ממוסך עם פיצוי סיבוב [0.0, 1.0]
double IrisMatcher::compare(const IrisCode& probe, const IrisCode& gallery) const
{
    double minDist = 1.0;
    for (int shift = -MAX_SHIFT; shift <= MAX_SHIFT; ++shift) {
        IrisCode shifted = cyclicShiftPositions(probe, shift);
        double d = hammingDistance(shifted, gallery);
        if (d < minDist) minDist = d;
    }
    return minDist;
}

//מבצע סיבובים מעגליים לקוד של הקשתית
IrisCode IrisMatcher::cyclicShiftPositions(const IrisCode& code, int s)
{
    if (s == 0) return code;
    IrisCode result = code;
    const int SAMPLES = 64;  // עמדות זוויתיות לכל רכיב פילטר
    int sh = ((s % SAMPLES) + SAMPLES) % SAMPLES;
    if (sh == 0) return result;

    // 32 קבוצות × 8 בייטים = 256 בייטים סה"כ
    for (int group = 0; group < 32; ++group) {
        int base = group * 8;

        // קריאת קבוצת 64 ביט בפורמט big-endian
        auto load64 = [&](const uint8_t* arr) -> uint64_t {
            uint64_t v = 0;
            for (int i = 0; i < 8; ++i)
                v = (v << 8) | arr[base + i];
            return v;
        };
        // כתיבת קבוצת 64 ביט בפורמט big-endian
        auto store64 = [&](uint8_t* arr, uint64_t v) {
            for (int i = 7; i >= 0; --i) {
                arr[base + i] = uint8_t(v & 0xFF);
                v >>= 8;
            }
        };
        uint64_t bv = load64(result.bits);
        uint64_t mv = load64(result.mask);
        // סיבוב ימינה ב-sh ביטים
        uint64_t bv2 = (bv >> sh) | (bv << (64 - sh));
        uint64_t mv2 = (mv >> sh) | (mv << (64 - sh));
        store64(result.bits, bv2);
        store64(result.mask, mv2);
    }
    return result;
}

//חישוב מרחק המינג בין שתי תבניות עם מסכה
double IrisMatcher::hammingDistance(const IrisCode& a, const IrisCode& b) const
{
    int diffBits  = 0;
    int validBits = 0;
    //רץ על כל 256 הבייטים של הקוד והמסכה
    for (int i = 0; i < 256; ++i) {
        uint8_t m  = a.mask[i] & b.mask[i];
        diffBits  += static_cast<int>(std::bitset<8>((a.bits[i] ^ b.bits[i]) & m).count());
        validBits += static_cast<int>(std::bitset<8>(m).count());
    }
    if (validBits < MIN_INTERSECTION_BITS) return 1.0;
    return static_cast<double>(diffBits) / validBits;
}

