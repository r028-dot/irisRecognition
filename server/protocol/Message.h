#pragma once
#include <cstdint>
#include <vector>

static constexpr uint32_t MSG_MAGIC   = 0x49524953u; // "IRIS"
static constexpr uint8_t  MSG_VERSION = 2;  // v2: נוסף timestamp+nonce למניעת Replay Attacks

enum class MessageType : uint8_t {
    VERIFY_REQUEST        = 0x01,  // אימות שתי עיניים יחד — ממוצע HD מול הסף
    ENROLL_REQUEST        = 0x02,
    VERIFY_RESPONSE       = 0x03,
    ENROLL_RESPONSE       = 0x04,
    ERROR_RESPONSE        = 0xFF
};

#pragma pack(push, 1)
struct MessageHeader {
    uint32_t    magic      = MSG_MAGIC;
    MessageType type       = MessageType::ERROR_RESPONSE;
    uint32_t    bodyLength = 0;
    uint8_t     version    = MSG_VERSION;
};
#pragma pack(pop)

// -------- Request bodies --------

// אימות שתי עיניים: 3 תמונות לכל עין; השרת מחשב ממוצע HD מול הסף.
// מבנה הנתונים שאחרי הכותרת:
//   left images (numLeftImages ciphertexts)  — גודל לפי leftImageSizes
//   right images (numRightImages ciphertexts) — גודל לפי rightImageSizes
static constexpr uint8_t MAX_VERIFY_IMAGES = 3;

struct VerifyRequest {
    char     passengerID[20]                    = {};   // מספר זהות
    char     gateName[10]                       = {};   // שם השער
    uint8_t  numLeftImages                      = 1;    // 1..MAX_VERIFY_IMAGES
    uint8_t  numRightImages                     = 1;    // 1..MAX_VERIFY_IMAGES
    uint8_t  _reserved[2]                       = {};
    uint64_t timestamp                          = 0;    // למניעת Replay Attacks
    uint8_t  nonce[16]                          = {};   // חד-פעמי
    uint8_t  iv[16]                             = {};   // IV משותף לכל התמונות
    uint32_t leftImageSizes[MAX_VERIFY_IMAGES]  = {};
    uint32_t rightImageSizes[MAX_VERIFY_IMAGES] = {};
    // followed by: left ciphertexts (numLeftImages), then right ciphertexts (numRightImages)
};

// רישום עם עד 3 תמונות לכל עין — כל תבנית נשמרת ב-DB בנפרד
static constexpr uint8_t MAX_ENROLL_IMAGES = 3;

struct EnrollRequest {
    char     passengerID[20]                    = {};
    char     fullName[100]                      = {};
    char     nationality[50]                    = {};
    uint8_t  iv[16]                             = {};  // IV משותף לכל התמונות
    uint8_t  numLeftImages                      = 1;   // 1..MAX_ENROLL_IMAGES
    uint8_t  numRightImages                     = 1;   // 1..MAX_ENROLL_IMAGES
    uint8_t  _reserved[2]                       = {};  // יישור ל-4 בתים
    uint32_t leftImageSizes[MAX_ENROLL_IMAGES]  = {};  // גודל ciphertext של כל תמונה שמאל
    uint32_t rightImageSizes[MAX_ENROLL_IMAGES] = {};  // גודל ciphertext של כל תמונה ימין
    // followed by: left images (numLeftImages), then right images (numRightImages)
};

// -------- Response bodies --------

struct VerifyResponse {
    uint8_t success          = 0;    // 1 = MATCH
    double  hammingDist      = 1.0;
    int32_t matchedUserID    = -1;
    char    matchedName[100] = {};
    char    flightNumber[16] = {};
    char    seatNumber[8]    = {};
    char    message[256]     = {};
};

struct EnrollResponse {
    uint8_t success   = 0;
    int32_t newUserID = -1;
    char    message[256] = {};
};

struct ErrorResponse {
    char message[256] = {};
};
