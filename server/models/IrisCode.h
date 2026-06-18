#pragma once
#include <cstdint>
#include <vector>
#include <bitset>
#include <cstring>
#include <stdexcept>
using namespace std;

//מחלקה אחראית לייצוג קוד איריס 
// IrisCode מייצג את תבנית האיריס של עין אחת, עם 2048 ביטים (256 בתים) של נתונים ו-256 ביטים של מסכה שמציינים אילו פיקסלים תקינים. יש פונקציה לחישוב מרחק המינג בין שני קודי איריס, ופונקציות לסיריאליזציה לדחיסה לאחסון במסד נתונים או לשליחה ברשת.
struct IrisCode {
    uint8_t bits[256] = {};  // 2048 ביטים של קוד איריס (32KB uncompressed), מאוחסנים כ-256 בתים (8 ביטים לכל בית)
    uint8_t mask[256] = {};  //   0 = פיקסל תקין= 1 , פיקסל פגום/מוסתר

   
    static constexpr int MIN_INTERSECTION_BITS = 400;

    double hammingDistance(const IrisCode& other) const {
        int diffBits  = 0;
        int validBits = 0;
        for (int i = 0; i < 256; ++i) {
            uint8_t m = mask[i] & other.mask[i];
            diffBits  += static_cast<int>(std::bitset<8>((bits[i] ^ other.bits[i]) & m).count());
            validBits += static_cast<int>(std::bitset<8>(m).count());
        }
        if (validBits < MIN_INTERSECTION_BITS) return 1.0;
        return static_cast<double>(diffBits) / validBits;
    }

    // Serialize: bits[256] || mask[256]  →  512 bytes
    vector<uint8_t> toBytes() const {
        vector<uint8_t> v(512);
        memcpy(v.data(),       bits, 256);
        memcpy(v.data() + 256, mask, 256);
        return v;
    }

    //פונקציה שאחראית ליצירת אובייקט איריס קוד
    //מקבלת מצביע לנתונים בינאריים ומחזירה אובייקט איריסקוד
    static IrisCode fromBytes(const uint8_t* data, size_t size) {
        if (size < 512)// אם גודל הנתונים קטן מ-512 בתים, זורק חריגה עם הודעה ברורה. 
            throw runtime_error("IrisCode::fromBytes: need 512 bytes");
        IrisCode code;
        memcpy(code.bits, data, 256);// העתקת 256 הבתים הראשונים ל-bits של האיריסקוד
        memcpy(code.mask, data + 256, 256);// העתקת 256 הבתים הבאים ל-mask של האיריסקוד
        return code;// החזרת האובייקט איריסקוד שנוצר
    }
};
