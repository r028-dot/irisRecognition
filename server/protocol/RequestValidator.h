#pragma once
#include <string>
#include <cstring>
#include <algorithm>
#include <cctype>
#include "../protocol/Message.h"

//ולידציות של בקשות VERIFY ו-ENROLL: מספר תמונות, גדלי תמונות, מזהה נוסע, פורמט הבקשה.
class RequestValidator {
public:

    // בודק שמספר התמונות תקין (1..MAX_VERIFY_IMAGES)
    static bool validateNumImages(uint8_t numImages, std::string& errMsg)
    {
        if (numImages < 1 || numImages > MAX_VERIFY_IMAGES) {
            errMsg = "VERIFY: numImages out of range (got "
                   + std::to_string(numImages) + ")";
            return false;
        }
        return true;
    }

    // בודק פורמט מזהה נוסע: 5-20 תווים אלפא-נומריים בלבד
    static bool validatePassengerID(const char* raw, size_t maxLen,
                                    std::string& parsed, std::string& errMsg)
    {
        parsed = std::string(raw, strnlen(raw, maxLen));
        if (parsed.size() < 5 || parsed.size() > 20) {
            errMsg = "VERIFY: passengerID length invalid (got "
                   + std::to_string(parsed.size()) + ")";
            return false;
        }
        bool allAlnum = std::all_of(parsed.begin(), parsed.end(),
                                    [](unsigned char c) { return std::isalnum(c) != 0; });
        if (!allAlnum) {
            errMsg = "VERIFY: passengerID contains illegal characters";
            return false;
        }
        return true;
    }

    // בודק שגדלי התמונות עקביים עם גוף הבקשה
    static bool validateImageSizes(const uint32_t* imageSizes,
                                   uint8_t numImages,
                                   size_t headerSize,
                                   uint32_t bodyLen,
                                   std::string& errMsg)
    {
        size_t offset = headerSize;
        for (uint8_t i = 0; i < numImages; ++i) {
            uint32_t sz = imageSizes[i];
            if (sz == 0 || offset + sz > static_cast<size_t>(bodyLen)) {
                errMsg = "VERIFY: inconsistent imageSizes["
                       + std::to_string(i) + "]";
                return false;
            }
            offset += sz;
        }
        return true;
    }

    // בודק שמספר התמונות לכל עין חוקי (1..MAX_ENROLL_IMAGES)
    static bool validateEnrollImageCounts(uint8_t numLeft, uint8_t numRight,
                                          std::string& errMsg)
    {
        if (numLeft < 1 || numLeft > MAX_ENROLL_IMAGES ||
            numRight < 1 || numRight > MAX_ENROLL_IMAGES) {
            errMsg = "ENROLL: numImages per eye must be 1.."
                   + std::to_string(MAX_ENROLL_IMAGES);
            return false;
        }
        return true;
    }
};
