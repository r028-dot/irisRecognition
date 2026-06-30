#pragma once
#include <cstdint>
#include <vector>

static constexpr uint32_t MSG_MAGIC = 0x49524953u; // "IRIS"
static constexpr uint8_t  MSG_VERSION = 2;  

enum class MessageType : uint8_t {
    VERIFY_REQUEST = 0x01,
    ENROLL_REQUEST = 0x02,
    VERIFY_RESPONSE = 0x03,
    ENROLL_RESPONSE = 0x04,
    ERROR_RESPONSE = 0xFF
};

#pragma pack(push, 1)
struct MessageHeader {
    uint32_t magic = MSG_MAGIC;
    MessageType type = MessageType::ERROR_RESPONSE;
    uint32_t bodyLength = 0;
    uint8_t version = MSG_VERSION;
};
#pragma pack(pop)

// שלח 1..3 תמונות לעין שמאל ו-1..3 תמונות לעין ימין.
// השרת מחשב ממוצע HD ומשווה לסף.
static constexpr uint8_t MAX_VERIFY_IMAGES = 3;

struct VerifyRequest {
    char passengerID[20] = {}; 
    char gateName[10] = {};   
    uint8_t numLeftImages = 1;   
    uint8_t numRightImages = 1;   
    uint8_t _reserved[2] = {};   
    uint64_t timestamp = 0;    
    uint8_t nonce[16] = {};  
    uint8_t iv[16] = {};   
    uint32_t leftImageSizes[MAX_VERIFY_IMAGES] = {};   
    uint32_t rightImageSizes[MAX_VERIFY_IMAGES] = {};   
};

// רישום עם עד 3 תמונות לכל עין — כל תבנית נשמרת ב-DB בנפרד
static constexpr uint8_t MAX_ENROLL_IMAGES = 3;

struct EnrollRequest {
    char passengerID[20] = {};
    char fullName[100] = {};
    char nationality[50] = {};
    uint8_t iv[16] = {}; 
    uint8_t numLeftImages = 1;
    uint8_t numRightImages = 1;
    uint8_t  _reserved[2]                       = {};
    uint32_t leftImageSizes[MAX_ENROLL_IMAGES]  = {}; 
    uint32_t rightImageSizes[MAX_ENROLL_IMAGES] = {};  
};

struct VerifyResponse {
    uint8_t success  = 0; 
    double  hammingDist = 1.0;
    int32_t matchedUserID = -1;
    char matchedName[100] = {};
    char flightNumber[16] = {};
    char seatNumber[8]    = {};
    char message[256]     = {};
};

struct EnrollResponse {
    uint8_t success   = 0;
    int32_t newUserID = -1;
    char    message[256] = {};
};

struct ErrorResponse {
    char message[256] = {};
};
