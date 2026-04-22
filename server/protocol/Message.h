#pragma once
#include <cstdint>
#include <vector>

static constexpr uint32_t MSG_MAGIC   = 0x49524953u; // "IRIS"
static constexpr uint8_t  MSG_VERSION = 1;

enum class MessageType : uint8_t {
    VERIFY_REQUEST   = 0x01,
    ENROLL_REQUEST   = 0x02,
    VERIFY_RESPONSE  = 0x03,
    ENROLL_RESPONSE  = 0x04,
    ERROR_RESPONSE   = 0xFF
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

struct VerifyRequest {
    char    passportNumber[20] = {};
    uint8_t eye                = 0;     // 0=Left, 1=Right
    uint8_t iv[16]             = {};
    uint32_t imageSize         = 0;
    // followed by: imageSize bytes of AES-encrypted iris image
};

struct EnrollRequest {
    char     passportNumber[20] = {};
    char     fullName[100]      = {};
    char     nationality[50]    = {};
    uint8_t  iv[16]             = {};
    uint32_t leftImageSize      = 0;
    // followed by: leftImageSize bytes (left eye), then right eye bytes
};

// -------- Response bodies --------

struct VerifyResponse {
    uint8_t success          = 0;    // 1 = MATCH
    double  hammingDist      = 1.0;
    int32_t matchedUserID    = -1;
    char    matchedName[100] = {};
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
