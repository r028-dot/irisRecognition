// ============================================================
//  ConnectionHandler.cpp
//  Handles one client connection: read → decrypt → process → respond
// ============================================================
#include "ConnectionHandler.h"
#include "../protocol/Message.h"
#include "../utils/Logger.h"
#include <stdexcept>
#include <cstring>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
ConnectionHandler::ConnectionHandler(SOCKET clientSocket,
                                     std::shared_ptr<IrisProcessor> processor,
                                     const Encryptor& encryptor)
    : m_socket(clientSocket)
    , m_processor(std::move(processor))
    , m_encryptor(encryptor)
{}

// ─────────────────────────────────────────────────────────────────────────────
// handle
// ─────────────────────────────────────────────────────────────────────────────
void ConnectionHandler::handle()
{
    try {
        MessageHeader hdr;
        recvAll(&hdr, sizeof(hdr));

        if (hdr.magic != MSG_MAGIC) {
            Logger::instance().warning("ConnectionHandler: bad magic, dropping connection");
            closesocket(m_socket);
            return;
        }

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

    closesocket(m_socket);
}

// ─────────────────────────────────────────────────────────────────────────────
// handleVerify
// ─────────────────────────────────────────────────────────────────────────────
void ConnectionHandler::handleVerify(uint32_t bodyLen)
{
    if (bodyLen < sizeof(VerifyRequest)) {
        sendError("VERIFY body too short");
        return;
    }

    std::vector<uint8_t> body(bodyLen);
    recvAll(body.data(), bodyLen);

    VerifyRequest req{};
    std::memcpy(&req, body.data(), sizeof(req));

    size_t cipherOffset = sizeof(VerifyRequest);
    size_t cipherSize   = bodyLen - cipherOffset;

    if (req.imageSize == 0 || cipherSize < req.imageSize) {
        sendError("VERIFY: inconsistent imageSize field");
        return;
    }

    // Build IV || ciphertext buffer for decryption
    std::vector<uint8_t> ivAndCipher(16 + cipherSize);
    std::memcpy(ivAndCipher.data(),      req.iv, 16);
    std::memcpy(ivAndCipher.data() + 16, body.data() + cipherOffset, cipherSize);

    std::vector<uint8_t> imageData = m_encryptor.decrypt(ivAndCipher);

    std::string passport(req.passportNumber,
                         strnlen(req.passportNumber, sizeof(req.passportNumber)));

    Logger::instance().info("VERIFY request: passport=" + passport
                            + " eye=" + std::to_string(req.eye));

    AuthResult result = m_processor->verify(passport, imageData, req.eye);

    VerifyResponse resp{};
    resp.success       = result.isMatch() ? 1 : 0;
    resp.hammingDist   = result.hammingDist;
    resp.matchedUserID = result.matchedUserID;
    std::strncpy(resp.matchedName, result.matchedName.c_str(),
                 sizeof(resp.matchedName) - 1);
    std::strncpy(resp.message, result.message.c_str(),
                 sizeof(resp.message) - 1);

    sendResponse(MessageType::VERIFY_RESPONSE, resp);

    Logger::instance().info("VERIFY result: " + result.message
                            + " HD=" + std::to_string(result.hammingDist));
}

// ─────────────────────────────────────────────────────────────────────────────
// handleEnroll
// ─────────────────────────────────────────────────────────────────────────────
void ConnectionHandler::handleEnroll(uint32_t bodyLen)
{
    if (bodyLen < sizeof(EnrollRequest)) {
        sendError("ENROLL body too short");
        return;
    }

    std::vector<uint8_t> body(bodyLen);
    recvAll(body.data(), bodyLen);

    EnrollRequest req{};
    std::memcpy(&req, body.data(), sizeof(req));

    size_t offset     = sizeof(EnrollRequest);
    size_t leftCipher = req.leftImageSize;
    if (offset + leftCipher > bodyLen) {
        sendError("ENROLL: inconsistent left image size");
        return;
    }
    size_t rightCipher = bodyLen - offset - leftCipher;

    // Decrypt left eye image
    std::vector<uint8_t> ivLeft(16 + leftCipher);
    std::memcpy(ivLeft.data(),      req.iv, 16);
    std::memcpy(ivLeft.data() + 16, body.data() + offset, leftCipher);
    std::vector<uint8_t> leftImage = m_encryptor.decrypt(ivLeft);

    // Decrypt right eye image
    std::vector<uint8_t> ivRight(16 + rightCipher);
    std::memcpy(ivRight.data(),      req.iv, 16);
    std::memcpy(ivRight.data() + 16, body.data() + offset + leftCipher, rightCipher);
    std::vector<uint8_t> rightImage = m_encryptor.decrypt(ivRight);

    std::string passport(req.passportNumber,
                         strnlen(req.passportNumber, sizeof(req.passportNumber)));
    std::string fullName(req.fullName,
                         strnlen(req.fullName, sizeof(req.fullName)));
    std::string nationality(req.nationality,
                            strnlen(req.nationality, sizeof(req.nationality)));

    Logger::instance().info("ENROLL request: passport=" + passport
                            + " name=" + fullName);

    AuthResult result = m_processor->enroll(passport, fullName, nationality,
                                             leftImage, rightImage);

    EnrollResponse resp{};
    resp.success   = result.isMatch() ? 1 : 0;
    resp.newUserID = result.matchedUserID;
    std::strncpy(resp.message, result.message.c_str(), sizeof(resp.message) - 1);

    sendResponse(MessageType::ENROLL_RESPONSE, resp);
    Logger::instance().info("ENROLL result: " + result.message);
}

// ─────────────────────────────────────────────────────────────────────────────
// sendResponse  (template + explicit instantiations)
// ─────────────────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
// sendError
// ─────────────────────────────────────────────────────────────────────────────
void ConnectionHandler::sendError(const std::string& msg) const
{
    ErrorResponse err{};
    std::strncpy(err.message, msg.c_str(), sizeof(err.message) - 1);
    sendResponse(MessageType::ERROR_RESPONSE, err);
}

// ─────────────────────────────────────────────────────────────────────────────
// I/O helpers
// ─────────────────────────────────────────────────────────────────────────────
void ConnectionHandler::sendAll(const void* data, size_t len) const
{
    const char* ptr  = reinterpret_cast<const char*>(data);
    size_t      sent = 0;
    while (sent < len) {
        int n = ::send(m_socket, ptr + sent,
                       static_cast<int>(len - sent), 0);
        if (n == SOCKET_ERROR)
            throw std::runtime_error("ConnectionHandler::sendAll: "
                                     + std::to_string(WSAGetLastError()));
        sent += static_cast<size_t>(n);
    }
}

void ConnectionHandler::recvAll(void* data, size_t len) const
{
    char*  ptr   = reinterpret_cast<char*>(data);
    size_t recvd = 0;
    while (recvd < len) {
        int n = ::recv(m_socket, ptr + recvd,
                       static_cast<int>(len - recvd), 0);
        if (n == 0)
            throw std::runtime_error(
                "ConnectionHandler::recvAll: connection closed by peer");
        if (n == SOCKET_ERROR)
            throw std::runtime_error("ConnectionHandler::recvAll: "
                                     + std::to_string(WSAGetLastError()));
        recvd += static_cast<size_t>(n);
    }
}
