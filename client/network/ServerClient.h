#pragma once
#include <openssl/ssl.h>
#include <vector>
#include <cstdint>
#include <string>
#include "../config/ClientConfig.h"

namespace iris {

// Result of a verify request — mirrors VerifyResponse on the wire.
enum class AuthStatus { AUTHORIZED, DENIED, COMM_ERROR };

struct AuthResponse {
    AuthStatus  status        = AuthStatus::COMM_ERROR;
    double      hammingDist   = 1.0;
    int32_t     matchedUserID = -1;
    std::string matchedName;
    std::string flightNumber;
    std::string seatNumber;
    std::string message;
};

// Result of an enroll request.
struct EnrollResult {
    bool        success   = false;
    int32_t     newUserID = -1;
    std::string message;
};

// Manages a one-shot TCP exchange with the iris recognition server.
// Each verify() call opens a fresh connection (matches server's accept loop).
class ServerClient {
public:
    explicit ServerClient(const ClientConfig& config);
    ~ServerClient();

    // אימות dual-eye: 3 תמונות שמאל + 3 תמונות ימין, השרת מחשב ממוצע HD
    AuthResponse verify(const std::string&                            passengerID,
                        const std::string&                            gateName,
                        const std::uint8_t                            iv[16],
                        const std::vector<std::vector<std::uint8_t>>& encryptedLeft,
                        const std::vector<std::vector<std::uint8_t>>& encryptedRight) const;
    EnrollResult enroll(const std::string&                            passengerID,
                        const std::string&                            fullName,
                        const std::string&                            nationality,
                        const std::uint8_t                            iv[16],
                        const std::vector<std::vector<std::uint8_t>>& encryptedLeft,
                        const std::vector<std::vector<std::uint8_t>>& encryptedRight) const;



private:
    SSL* connectToServer() const;
    void sendAll(SSL* ssl, const void* buf, std::size_t len) const;
    void recvAll(SSL* ssl, void*       buf, std::size_t len) const;

    SSL_CTX*    m_ctx = nullptr;   // הקשר TLS של הלקוח (מאמת את תעודת השרת)
    std::string m_host;
    int         m_port;
};

} // namespace iris
