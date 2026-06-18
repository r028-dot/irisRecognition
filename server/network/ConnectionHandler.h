#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <openssl/ssl.h>
#include <memory>
#include "../service/BiometricService.h"
#include "../security/Encryptor.h"
#include "../protocol/Message.h"

struct ReplayGuard;  

// רכיב הטיפול בבקשות (ConnectionHandler): מנהל את מחזור החיים של בקשת נוסע בודד, החל מלחיצת היד המוצפנת ועד לעיבוד התמונות ושליחת התוצאה.
class ConnectionHandler {
public:
    //בנאי מקבל את כל המידע הדרוש לטיפול בבקשה
    ConnectionHandler(SSL* ssl,
                      SOCKET clientSocket,
                      std::shared_ptr<BiometricService> service,
                      const Encryptor& encryptor,
                      ReplayGuard& replayGuard);

    void handle();// מטפל בחיבור: TLS handshake → קרא בקשה → עבד → שלח תשובה. זורק חריגה אם יש בעיה. 

private:
    SSL* m_ssl;   
    SOCKET m_socket;
    shared_ptr<BiometricService> m_processor;
    const Encryptor& m_encryptor;
    ReplayGuard& m_replayGuard;  

  
    void sendAll(const void* data, size_t len) const;
    void recvAll(void* data, size_t len) const;

    void handleVerify(uint32_t bodyLen);
    void handleEnroll(uint32_t bodyLen);

    template<typename T>
    void sendResponse(MessageType type, const T& body) const;

    void sendError(const std::string& msg) const;
};
