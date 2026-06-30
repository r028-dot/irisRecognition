#pragma once
#include <cstdint>
#include <vector>
#include <cstring>
#include <stdexcept>
using namespace std;

// ייצוג קוד ביומטרי של קשתית — 2048 ביטים + מסכת תקינות
struct IrisCode {
    uint8_t bits[256] = {};  // 2048 ביטים של קוד קשתית, 8 ביטים לבית
    uint8_t mask[256] = {};  // 1 = פיקסל תקין, 0 = פיקסל פגום/מוסתר

    // סריאליזציה: bits[256] || mask[256] → 512 בתים
    vector<uint8_t> toBytes() const;

    // דה-סריאליזציה מ-512 בתים
    static IrisCode fromBytes(const uint8_t* data, size_t size);
};
