
#include "ConnectionHandler.h"
#include "../security/ReplayGuard.h"
#include "../protocol/Message.h"
#include "../protocol/RequestValidator.h"
#include "../utils/Logger.h"
#include <openssl/err.h>
#include <stdexcept>
#include <cstring>
#include <vector>
#include <algorithm>
using namespace std;
// בנאי המחלקה (Constructor): מקבל ומאתחל את ערוץ ה-SSL, הסוקט, השירות הביומטרי ורכיבי האבטחה עבור החיבור הנוכחי.
ConnectionHandler::ConnectionHandler(SSL* ssl,
                                     SOCKET clientSocket,
                                     shared_ptr<BiometricService> service,
                                     const Encryptor& encryptor,
                                     ReplayGuard& replayGuard)
    : m_ssl(ssl)
    , m_socket(clientSocket)
    , m_processor(std::move(service))
    , m_encryptor(encryptor)
    , m_replayGuard(replayGuard)
{}

// ניהול מחזור החיים של הבקשה: ביצוע לחיצת יד TLS, קריאת הכותרת (Header) וניתוב הבקשה לפונקציה המתאימה.
void ConnectionHandler::handle()
{
    //כאן מתבצעת לחיצת היד של TLS. אם היא נכשלת, מתריע בלוג ומסיים את הטיפול בחיבור.
    if (SSL_accept(m_ssl) <= 0) {
        Logger::instance().warning("ConnectionHandler: TLS handshake failed");
        SSL_free(m_ssl);
        closesocket(m_socket);
        return;
    }

    try {
        MessageHeader hdr;
        recvAll(&hdr, sizeof(hdr));
        //בודק האם הכותרת חוקית (Magic Number) ואם לא, מסיים את החיבור.
        if (hdr.magic != MSG_MAGIC) {
            Logger::instance().warning("ConnectionHandler: bad magic, dropping connection");
            SSL_shutdown(m_ssl);
            SSL_free(m_ssl);
            closesocket(m_socket);
            return;
        }

        // מנתב את הבקשה לפונקציה המתאימה לפי סוג ההודעה
        switch (hdr.type) {
            case MessageType::VERIFY_REQUEST:
                handleVerify(hdr.bodyLength);
                break;
            case MessageType::ENROLL_REQUEST:
                handleEnroll(hdr.bodyLength);
                break;
            default:
                sendError("Unknown message type");
                break;
        }
    }
    catch (const std::exception& e) {
        Logger::instance().error(
            std::string("ConnectionHandler exception: ") + e.what());
        try { sendError(e.what()); } catch (...) {}
    }
    // סיום החיבור: סוגר את ערוץ ה-TLS ואת הסוקט
    SSL_shutdown(m_ssl);
    SSL_free(m_ssl);
    closesocket(m_socket);
}


// ניהול בקשת ENROLL: קריאת הבקשה, ולידציה, פענוח תמונות, עיבוד ושליחת התשובה.
void ConnectionHandler::handleEnroll(uint32_t bodyLen)
{
    if (bodyLen < sizeof(EnrollRequest)) {
        sendError("ENROLL body too short");
        return;
    }
    vector<uint8_t> body(bodyLen);
    recvAll(body.data(), bodyLen);
    EnrollRequest req{};
    memcpy(&req, body.data(), sizeof(req));

    // ולידציה: חייב בדיוק MAX_ENROLL_IMAGES תמונות לכל עין
    if (req.numLeftImages != MAX_ENROLL_IMAGES ||
        req.numRightImages != MAX_ENROLL_IMAGES) {
        sendError("ENROLL: exactly " + std::to_string(MAX_ENROLL_IMAGES) +
                  " images per eye are required");
        return;
    }

    string passport(req.passengerID, strnlen(req.passengerID, sizeof(req.passengerID)));
    string fullName(req.fullName, strnlen(req.fullName, sizeof(req.fullName)));
    string nationality(req.nationality, strnlen(req.nationality, sizeof(req.nationality)));

    size_t offset = sizeof(EnrollRequest);

    // פענוח תמונות עין שמאל
    vector<vector<uint8_t>> leftImages, rightImages;
    for (uint8_t i = 0; i < req.numLeftImages; ++i) {
        uint32_t sz = req.leftImageSizes[i];
        if (sz == 0 || offset + sz > bodyLen) {
            sendError("ENROLL: left image size invalid at index " + std::to_string(i));
            return;
        }
        vector<uint8_t> ivAndCipher(16 + sz);
        memcpy(ivAndCipher.data(),      req.iv, 16);
        memcpy(ivAndCipher.data() + 16, body.data() + offset, sz);
        leftImages.push_back(m_encryptor.decrypt(ivAndCipher));
        offset += sz;
    }

    // פענוח תמונות עין ימין
    for (uint8_t i = 0; i < req.numRightImages; ++i) {
        uint32_t sz = req.rightImageSizes[i];
        if (sz == 0 || offset + sz > bodyLen) {
            sendError("ENROLL: right image size invalid at index " + std::to_string(i));
            return;
        }
        vector<uint8_t> ivAndCipher(16 + sz);
        memcpy(ivAndCipher.data(),      req.iv, 16);
        memcpy(ivAndCipher.data() + 16, body.data() + offset, sz);
        rightImages.push_back(m_encryptor.decrypt(ivAndCipher));
        offset += sz;
    }

    Logger::instance().info("ENROLL request: passport=" + passport + " name=" + fullName
                            + " leftImages=" + std::to_string(req.numLeftImages)
                            + " rightImages=" + std::to_string(req.numRightImages));

    AuthResult result = m_processor->enroll(passport, fullName, nationality,
                                            leftImages, rightImages);
    EnrollResponse resp{};
    resp.success   = result.isMatch() ? 1 : 0;
    resp.newUserID = result.matchedUserID;
    strncpy(resp.message, result.message.c_str(), sizeof(resp.message) - 1);
    sendResponse(MessageType::ENROLL_RESPONSE, resp);
    Logger::instance().info("ENROLL result: " + result.message);
}

//ביצוע אימות: קריאת הבקשה, ולידציה, פענוח תמונות, עיבוד ושליחת התשובה.
void ConnectionHandler::handleVerify(uint32_t bodyLen)
{
    if (bodyLen < sizeof(VerifyRequest)) {
        sendError("VERIFY: body too small");
        return;
    }

    vector<uint8_t> body(bodyLen);
    recvAll(body.data(), bodyLen);

    VerifyRequest req{};
    memcpy(&req, body.data(), sizeof(req));

    // ולידציה
    string passport;
    {
        string errMsg;
        if (!RequestValidator::validatePassengerID(
                req.passengerID, sizeof(req.passengerID), passport, errMsg)) {
            sendError(errMsg);
            return;
        }
    }
    if (!m_replayGuard.checkAndRegister(req.nonce, req.timestamp)) {
        sendError("VERIFY: request expired or already processed (replay protection)");
        return;
    }
    if (req.numLeftImages != MAX_VERIFY_IMAGES || req.numRightImages != MAX_VERIFY_IMAGES) {
        sendError("VERIFY: exactly 3 images per eye are required");
        return;
    }

    string gateName(req.gateName, strnlen(req.gateName, sizeof(req.gateName)));
    if (gateName.empty()) {
        sendError("VERIFY: gate name is required");
        return;
    }
    Logger::instance().info("VERIFY request: passport=" + passport
                            + " gate=" + gateName
                            + " leftImages=" + std::to_string(req.numLeftImages)
                            + " rightImages=" + std::to_string(req.numRightImages));

    // פענוח תמונות שמאל
    vector<vector<uint8_t>> leftImages, rightImages;
    size_t offset = sizeof(VerifyRequest);
    for (uint8_t i = 0; i < req.numLeftImages; ++i) {
        uint32_t sz = req.leftImageSizes[i];
        if (sz == 0 || offset + sz > bodyLen) { sendError("VERIFY: left image size invalid"); return; }
        vector<uint8_t> ivAndCipher(16 + sz);
        memcpy(ivAndCipher.data(),      req.iv, 16);
        memcpy(ivAndCipher.data() + 16, body.data() + offset, sz);
        leftImages.push_back(m_encryptor.decrypt(ivAndCipher));
        offset += sz;
    }
    // פענוח תמונות ימין
    for (uint8_t i = 0; i < req.numRightImages; ++i) {
        uint32_t sz = req.rightImageSizes[i];
        if (sz == 0 || offset + sz > bodyLen) { sendError("VERIFY: right image size invalid"); return; }
        vector<uint8_t> ivAndCipher(16 + sz);
        memcpy(ivAndCipher.data(),      req.iv, 16);
        memcpy(ivAndCipher.data() + 16, body.data() + offset, sz);
        rightImages.push_back(m_encryptor.decrypt(ivAndCipher));
        offset += sz;
    }

    AuthResult result = m_processor->verifyForGate(
        passport, gateName, leftImages, rightImages);

    VerifyResponse resp{};
    resp.success = result.isMatch() ? 1 : 0;
    resp.hammingDist = result.hammingDist;
    resp.matchedUserID = result.matchedUserID;
    strncpy(resp.matchedName, result.matchedName.c_str(), sizeof(resp.matchedName) - 1);
    strncpy(resp.flightNumber, result.flightNumber.c_str(), sizeof(resp.flightNumber) - 1);
    strncpy(resp.seatNumber, result.seatNumber.c_str(), sizeof(resp.seatNumber) - 1);
    strncpy(resp.message, result.message.c_str(), sizeof(resp.message) - 1);
    sendResponse(MessageType::VERIFY_RESPONSE, resp);
    Logger::instance().info("VERIFY result: " + result.message);
}

// פונקציה עזר לשליחת הודעת שגיאה: יוצרת מבנה ErrorResponse עם ההודעה ומעבירה ל-sendResponse.
void ConnectionHandler::sendError(const std::string& msg) const
{
    ErrorResponse err{};
    strncpy(err.message, msg.c_str(), sizeof(err.message) - 1);
    sendResponse(MessageType::ERROR_RESPONSE, err);
}

// תבנית פונקציה לשליחת תשובה: יוצרת כותרת (Header) ושולחת את הכותרת והגוף (Body) דרך ערוץ ה-SSL.
template<typename T>
void ConnectionHandler::sendResponse(MessageType type, const T& body) const
{
    MessageHeader hdr{};
    hdr.magic      = MSG_MAGIC;
    hdr.type       = type;
    hdr.bodyLength = static_cast<uint32_t>(sizeof(T));
    hdr.version    = MSG_VERSION;

    sendAll(&hdr,  sizeof(hdr));
    sendAll(&body, sizeof(T));
}

template void ConnectionHandler::sendResponse<VerifyResponse>(
    MessageType, const VerifyResponse&) const;
template void ConnectionHandler::sendResponse<EnrollResponse>(
    MessageType, const EnrollResponse&) const;
template void ConnectionHandler::sendResponse<ErrorResponse>(
    MessageType, const ErrorResponse&) const;



// שליחה מאובטחת של חבילת מידע בשלמותה (מניעת שברים ברשת)
void ConnectionHandler::sendAll(const void* data, size_t len) const
{
    const char* ptr  = reinterpret_cast<const char*>(data);
    size_t      sent = 0;
    while (sent < len) {
        int n = SSL_write(m_ssl, ptr + sent,
                          static_cast<int>(len - sent));
        if (n <= 0)
            throw std::runtime_error("ConnectionHandler::sendAll: "
                                     + std::to_string(WSAGetLastError()));
        sent += static_cast<size_t>(n);
    }
}

// קליטה ופענוח של מידע מוצפן עד להשלמת החבילה במלואה
void ConnectionHandler::recvAll(void* data, size_t len) const
{
    char*  ptr   = reinterpret_cast<char*>(data);
    size_t recvd = 0;
    while (recvd < len) {
        int n = SSL_read(m_ssl, ptr + recvd,
                         static_cast<int>(len - recvd));
        if (n <= 0)
            throw std::runtime_error(
                "ConnectionHandler::recvAll: connection closed by peer");
        if (n == SOCKET_ERROR)
            throw std::runtime_error("ConnectionHandler::recvAll: "
                                     + std::to_string(WSAGetLastError()));
        recvd += static_cast<size_t>(n);
    }
}
