#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <memory>
#include "../iris/IrisProcessor.h"
#include "../security/Encryptor.h"
#include "../protocol/Message.h"

// Handles a single client connection end-to-end:
//   1. Receive MessageHeader
//   2. Receive body (AES-encrypted iris image + metadata)
//   3. Decrypt image
//   4. Delegate to IrisProcessor (verify / enroll)
//   5. Send response MessageHeader + body
//   6. Close socket
class ConnectionHandler {
public:
    ConnectionHandler(SOCKET clientSocket,
                      std::shared_ptr<IrisProcessor> processor,
                      const Encryptor& encryptor);

    void handle();   // Called from a thread-pool worker thread

private:
    SOCKET                         m_socket;
    std::shared_ptr<IrisProcessor> m_processor;
    const Encryptor&               m_encryptor;

    // I/O helpers – throw on error
    void sendAll(const void* data, size_t len) const;
    void recvAll(void*       data, size_t len) const;

    // Per-message-type handlers
    void handleVerify(uint32_t bodyLen);
    void handleEnroll(uint32_t bodyLen);

    // Send a typed response
    template<typename T>
    void sendResponse(MessageType type, const T& body) const;

    void sendError(const std::string& msg) const;
};
