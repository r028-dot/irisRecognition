#include "ServerClient.h"
#include "../protocol/Message.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <bcrypt.h>          
#include <chrono>          
#include <cstdlib>        
#include <stdexcept>
#include <cstring>
#include <string>
#include <cassert>

#pragma comment(lib, "bcrypt.lib")

#pragma comment(lib, "ws2_32.lib")
using namespace std;
namespace iris {

//בנאי המחלקה: מאתחל את כתובת השרת והפורט, מעלה את ספריית Winsock ומאתחל את TLS בצד לקוח.
ServerClient::ServerClient(const ClientConfig& config)
    : m_host(config.serverHost), m_port(config.serverPort)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        throw std::runtime_error("ServerClient: WSAStartup failed");

    //  אתחול TLS בצד לקוח 
    m_ctx = SSL_CTX_new(TLS_client_method());
    if (!m_ctx)
        throw runtime_error("ServerClient: SSL_CTX_new failed");

    SSL_CTX_set_min_proto_version(m_ctx, TLS1_2_VERSION);

    // אימות תעודת השרת מול תעודה "נעוצה" (certificate pinning): הלקוח סומך
    // אך ורק על התעודה המקומית server.crt. כך לא ניתן להתחזות לשרת גם אם
    // תוקף שולט בערוץ הרשת (הגנה מפני Man-in-the-Middle).
    const char* caEnv = getenv("IRIS_TLS_CA");
    const string caPath = (caEnv && *caEnv) ? string(caEnv) : string("server.crt");
    if (SSL_CTX_load_verify_locations(m_ctx, caPath.c_str(), nullptr) != 1) {
        SSL_CTX_free(m_ctx);
        m_ctx = nullptr;
        throw runtime_error("ServerClient: failed to load server certificate: " + caPath);
    }
    SSL_CTX_set_verify(m_ctx, SSL_VERIFY_PEER, nullptr);
}
//  מחלקת ServerClient: משחררת את הקשר TLS ומנקה את ספריית Winsock.
ServerClient::~ServerClient()
{
    if (m_ctx) {
        SSL_CTX_free(m_ctx);
        m_ctx = nullptr;
    }
    WSACleanup();
}

// חיבור לשרת 
SSL* ServerClient::connectToServer() const
{
    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    if (getaddrinfo(m_host.c_str(), to_string(m_port).c_str(),
                    &hints, &res) != 0)
        throw runtime_error("getaddrinfo failed for host: " + m_host);

    if (!res)
        throw runtime_error("getaddrinfo returned null result");

    assert(res != nullptr); 
    SOCKET sock = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == INVALID_SOCKET) {
        freeaddrinfo(res);
        throw runtime_error("socket() failed");
    }

    if (!res->ai_addr || res->ai_addrlen == 0) {
        freeaddrinfo(res);
        closesocket(sock);
        throw runtime_error("invalid address info from getaddrinfo");
    }
    assert(res->ai_addr != nullptr); 
    if (::connect(sock, res->ai_addr,
                  static_cast<int>(res->ai_addrlen)) != 0) {
        freeaddrinfo(res);
        closesocket(sock);
        throw runtime_error("connect() failed to " +
                                  m_host + ":" + to_string(m_port));
    }

    freeaddrinfo(res);

    // הקמת SSL מעל ה-socket המחובר
    SSL* ssl = SSL_new(m_ctx);
    if (!ssl) {
        closesocket(sock);
        throw runtime_error("SSL_new failed");
    }
    SSL_set_fd(ssl, static_cast<int>(sock));
    if (SSL_connect(ssl) != 1) {
        SSL_free(ssl);
        closesocket(sock);
        throw runtime_error("TLS handshake (SSL_connect) failed");
    }
    // אימות התעודה (SSL_VERIFY_PEER מפיל את ה-handshake בכישלון; בודקים שוב במפורש)
    if (SSL_get_verify_result(ssl) != X509_V_OK) {
        SSL_free(ssl);
        closesocket(sock);
        throw runtime_error("TLS certificate verification failed");
    }
    return ssl;
}

//  sendAll: שולח את כל הנתונים ב-buffer דרך ערוץ ה-SSL, עד להשלמתם.
void ServerClient::sendAll(SSL* ssl, const void* buf, std::size_t len) const
{
    const char* p   = static_cast<const char*>(buf);
    std::size_t off = 0;
    while (off < len) {
        int n = SSL_write(ssl, p + off, static_cast<int>(len - off));
        if (n <= 0) {
            int err = SSL_get_error(ssl, n);
            char errBuf[256] = {};
            ERR_error_string_n(ERR_get_error(), errBuf, sizeof(errBuf));
            throw runtime_error(string("send() failed (TLS): SSL_error=") + 
                              to_string(err) + " " + errBuf);
        }
        off += static_cast<std::size_t>(n);
    }
}

//  recvAll: קורא את כל הנתונים מהערוץ ה-SSL ל-buffer, עד להשלמתם.
void ServerClient::recvAll(SSL* ssl, void* buf, std::size_t len) const
{
    char* p = static_cast<char*>(buf);
    std::size_t off = 0;
    while (off < len) 
    {
        int n = SSL_read(ssl, p + off, static_cast<int>(len - off));
        if (n <= 0)
            throw std::runtime_error("recv() failed or connection closed (TLS)");
        off += static_cast<std::size_t>(n);
    }
}

//  enroll 
// שולח עד MAX_ENROLL_IMAGES תמונות לכל עין לשרת לצורך רישום ביומטרי.
// כל התמונות מוצפנות AES-CBC עם אותו IV.
EnrollResult ServerClient::enroll(const string& passengerID,
                                   const string& fullName,
                                   const string& nationality,
                                   const uint8_t iv[16],
                                   const vector<vector<uint8_t>>& encryptedLeft,
                                   const vector<vector<uint8_t>>& encryptedRight) const
{
    EnrollResult result;
    if (encryptedLeft.empty() || encryptedLeft.size() > MAX_ENROLL_IMAGES ||
        encryptedRight.empty() || encryptedRight.size() > MAX_ENROLL_IMAGES) {
        result.message = "enroll: invalid image count (1.." +
                         to_string(MAX_ENROLL_IMAGES) + " per eye)";
        return result;
    }

    SSL* ssl = nullptr;
    try {
        ssl = connectToServer();
        // בניית EnrollRequest
        EnrollRequest req{};
        strncpy(req.passengerID, passengerID.c_str(), sizeof(req.passengerID) - 1);
        strncpy(req.fullName, fullName.c_str(), sizeof(req.fullName) - 1);
        strncpy(req.nationality, nationality.c_str(), sizeof(req.nationality) - 1);
        memcpy(req.iv, iv, 16);
        req.numLeftImages  = static_cast<uint8_t>(encryptedLeft.size());
        req.numRightImages = static_cast<uint8_t>(encryptedRight.size());

        // חישוב גדלי ה-ciphertext
        size_t totalPayload = 0;
        for (size_t i = 0; i < encryptedLeft.size(); ++i) {
            req.leftImageSizes[i] = static_cast<uint32_t>(encryptedLeft[i].size());
            totalPayload += encryptedLeft[i].size();
        }
        for (size_t i = 0; i < encryptedRight.size(); ++i) {
            req.rightImageSizes[i] = static_cast<uint32_t>(encryptedRight[i].size());
            totalPayload += encryptedRight[i].size();
        }

        // כותרת
        MessageHeader hdr{};
        hdr.magic = MSG_MAGIC;
        hdr.type = MessageType::ENROLL_REQUEST;
        hdr.bodyLength = static_cast<uint32_t>(sizeof(EnrollRequest) + totalPayload);
        hdr.version = MSG_VERSION;

        sendAll(ssl, &hdr, sizeof(hdr));
        sendAll(ssl, &req, sizeof(req));

        // שליחת תמונות שמאל
        for (const auto& img : encryptedLeft)
            sendAll(ssl, img.data(), img.size());

        // שליחת תמונות ימין
        for (const auto& img : encryptedRight)
            sendAll(ssl, img.data(), img.size());

        // קריאת תגובה
        MessageHeader respHdr{};
        recvAll(ssl, &respHdr, sizeof(respHdr));
        if (respHdr.magic != MSG_MAGIC)
            throw runtime_error("enroll: bad magic in response");

        if (respHdr.type == MessageType::ERROR_RESPONSE) {
            ErrorResponse err{};
            recvAll(ssl, &err, sizeof(err));
            result.message = string(err.message,
                                         strnlen(err.message, sizeof(err.message)));
        } else if (respHdr.type == MessageType::ENROLL_RESPONSE) {
            EnrollResponse resp{};
            recvAll(ssl, &resp, sizeof(resp));
            result.success = (resp.success == 1);
            result.newUserID = resp.newUserID;
            result.message = string(resp.message,
                                            strnlen(resp.message, sizeof(resp.message)));
        } else {
            throw runtime_error("enroll: unexpected response type");
        }
    }
    catch (const std::exception& e) {
        result.message = e.what();
    }

    if (ssl) {
        int fd = SSL_get_fd(ssl);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        if (fd >= 0) closesocket(static_cast<SOCKET>(fd));
    }
    return result;
}

//  verify 
// שולח שתי עיניים בפנייה אחת; השרת מחשב ממוצע HD ומשווה לסף.
AuthResponse ServerClient::verify(
    const string& passengerID,
    const string& gateName,
    const uint8_t iv[16],
    const vector<vector<uint8_t>>& encryptedLeft,
    const vector<vector<uint8_t>>& encryptedRight) const
{
    AuthResponse resp;
    if (gateName.empty()) {
        resp.message = "verify: gate name is required";
        return resp;
    }
    if (encryptedLeft.size() != MAX_VERIFY_IMAGES ||
        encryptedRight.size() != MAX_VERIFY_IMAGES) {
        resp.message = "verify: invalid image count";
        return resp;
    }
    //  חיבור לשרת
    SSL* ssl = nullptr;
    try {
        ssl = connectToServer();
        // בניית VerifyRequest לשתי עיניים בפנייה אחת
        VerifyRequest req{};
        std::strncpy(req.passengerID, passengerID.c_str(), sizeof(req.passengerID) - 1);
        std::strncpy(req.gateName,    gateName.c_str(),    sizeof(req.gateName)    - 1);
        req.numLeftImages  = static_cast<uint8_t>(encryptedLeft.size());
        req.numRightImages = static_cast<uint8_t>(encryptedRight.size());
        std::memcpy(req.iv, iv, 16);
        // הגדרת timestamp ו-nonce למניעת Replay Attacks
        req.timestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        BCryptGenRandom(nullptr, req.nonce, sizeof(req.nonce),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG);

        uint32_t totalCipher = 0;
        for (size_t i = 0; i < encryptedLeft.size();  ++i) {
            req.leftImageSizes[i] = static_cast<uint32_t>(encryptedLeft[i].size());
            totalCipher += req.leftImageSizes[i];
        }
        for (size_t i = 0; i < encryptedRight.size(); ++i) {
            req.rightImageSizes[i]  = static_cast<uint32_t>(encryptedRight[i].size());
            totalCipher += req.rightImageSizes[i];
        }

        const uint32_t bodyLen =
            static_cast<uint32_t>(sizeof(req)) + totalCipher;

        MessageHeader hdr{};
        hdr.magic      = MSG_MAGIC;
        hdr.type       = MessageType::VERIFY_REQUEST;
        hdr.bodyLength = bodyLen;
        hdr.version    = MSG_VERSION;

        // שולחים כותרת + VerifyRequest + כל התמונות (שתי עיניים)
        sendAll(ssl, &hdr, sizeof(hdr));
        sendAll(ssl, &req, sizeof(req));
        for (const auto& img : encryptedLeft)
            sendAll(ssl, img.data(), img.size());
        for (const auto& img : encryptedRight)
            sendAll(ssl, img.data(), img.size());

        // קריאת תגובה
        MessageHeader respHdr{};
        recvAll(ssl, &respHdr, sizeof(respHdr));
        if (respHdr.magic != MSG_MAGIC)
            throw runtime_error("verify: bad magic in response");

        if (respHdr.type == MessageType::ERROR_RESPONSE) {
            ErrorResponse err{};
            recvAll(ssl, &err, sizeof(err));
            resp.status  = AuthStatus::COMM_ERROR;
            resp.message = string(err.message,
                                       strnlen(err.message, sizeof(err.message)));
        } else if (respHdr.type == MessageType::VERIFY_RESPONSE) {
            VerifyResponse v{};
            recvAll(ssl, &v, sizeof(v));
            resp.status = (v.success == 1) ? AuthStatus::AUTHORIZED : AuthStatus::DENIED;
            resp.hammingDist   = v.hammingDist;
            resp.matchedUserID = v.matchedUserID;
            resp.matchedName = string(v.matchedName, strnlen(v.matchedName, sizeof(v.matchedName)));
            resp.flightNumber = string(v.flightNumber, strnlen(v.flightNumber, sizeof(v.flightNumber)));
            resp.seatNumber = string(v.seatNumber, strnlen(v.seatNumber, sizeof(v.seatNumber)));
            resp.message = string(v.message, strnlen(v.message, sizeof(v.message)));
        } else {
            throw runtime_error("verify: unexpected response type");
        }
    }
    catch (const std::exception& e) {
        resp.status  = AuthStatus::COMM_ERROR;
        resp.message = e.what();
    }
    // ניקוי TLS וסוקט
    if (ssl) {
        int fd = SSL_get_fd(ssl);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        if (fd >= 0) closesocket(static_cast<SOCKET>(fd));
    }
    return resp;
}

} 
