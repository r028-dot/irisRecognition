#include "ServerClient.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdexcept>
#include <cstring>
using namespace std;

#pragma comment(lib, "ws2_32.lib")

namespace iris {

// ── פורמט הודעה (לפי Message.h) ───────────────────────────────────────────
// MessageHeader: magic(4B) + type(1B) + bodyLength(4B, network order) + version(1B)
static const uint8_t  MSG_MAGIC[4]      = { 'I', 'R', 'I', 'S' };
static const uint8_t  PROTOCOL_VERSION  = 1;

enum MsgType : uint8_t {
    VERIFY_REQUEST  = 1,
    VERIFY_RESPONSE = 3,
    ERROR_RESPONSE  = 5,
};

#pragma pack(push, 1)
struct MessageHeader {
    uint8_t  magic[4];
    uint8_t  type;
    uint32_t bodyLength;  // big-endian (network byte order)
    uint8_t  version;
};
#pragma pack(pop)

// ── בנאי / מפרק ───────────────────────────────────────────────────────────
ServerClient::ServerClient(const ClientConfig& config)
    : m_host(config.serverHost), m_port(config.serverPort)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        throw runtime_error("WSAStartup failed");
}

ServerClient::~ServerClient()
{
    WSACleanup();
}

// ── sendIrisImage ──────────────────────────────────────────────────────────
// שולח תמונת קשתית מוצפנת לשרת ומחזיר את תוצאת הזיהוי
AuthResponse ServerClient::sendIrisImage(const std::vector<uint8_t>& encryptedImage) const
{
    AuthResponse resp;  // ברירת מחדל: ERROR

    // 1. פתיחת חיבור TCP
    int sock;
    try {
        sock = connectToServer();
    } catch (const std::exception& e) {
        resp.message = e.what();
        return resp;
    }

    try {
        // 2. בניית ושליחת הודעת VERIFY_REQUEST
        MessageHeader hdr;
        memcpy(hdr.magic, MSG_MAGIC, 4);
        hdr.type       = VERIFY_REQUEST;
        hdr.bodyLength = htonl(static_cast<uint32_t>(encryptedImage.size()));
        hdr.version    = PROTOCOL_VERSION;

        sendAll(sock, reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));
        sendAll(sock, encryptedImage.data(), encryptedImage.size());

        // 3. קריאת header של התגובה
        MessageHeader respHdr;
        recvAll(sock, reinterpret_cast<uint8_t*>(&respHdr), sizeof(respHdr));

        // אימות magic
        if (memcmp(respHdr.magic, MSG_MAGIC, 4) != 0)
            throw runtime_error("Invalid response magic");

        uint32_t bodyLen = ntohl(respHdr.bodyLength);

        // שגיאה מהשרת
        if (respHdr.type == ERROR_RESPONSE) {
            resp.message = "Server returned error";
            closesocket(sock);
            return resp;
        }

        if (bodyLen == 0)
            throw runtime_error("Empty response body");

        // 4. קריאת גוף התגובה: byte[0]=status, byte[1..]=הודעה אופציונלית
        std::vector<uint8_t> body(bodyLen);
        recvAll(sock, body.data(), bodyLen);

        switch (body[0]) {
            case 0:  resp.status = AuthStatus::AUTHORIZED; break;
            case 1:  resp.status = AuthStatus::DENIED;     break;
            default: resp.status = AuthStatus::COMM_ERROR;      break;
        }

        if (bodyLen > 1)
            resp.message = std::string(body.begin() + 1, body.end());

    } catch (const std::exception& e) {
        resp.message = e.what();
        closesocket(sock);
        return resp;
    }

    closesocket(sock);
    return resp;
}

// ── connectToServer ────────────────────────────────────────────────────────
// פותח שקע TCP לשרת; זורק חריג אם נכשל
int ServerClient::connectToServer() const
{
    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    if (getaddrinfo(m_host.c_str(), std::to_string(m_port).c_str(), &hints, &res) != 0)
        throw runtime_error("getaddrinfo failed for host: " + m_host);

    SOCKET sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == INVALID_SOCKET) {
        freeaddrinfo(res);
        throw runtime_error("socket() failed");
    }

    if (connect(sock, res->ai_addr, static_cast<int>(res->ai_addrlen)) != 0) {
        freeaddrinfo(res);
        closesocket(sock);
        throw runtime_error("connect() failed to " +
                            m_host + ":" + to_string(m_port));
    }

    freeaddrinfo(res);
    return static_cast<int>(sock);
}

// ── sendAll ────────────────────────────────────────────────────────────────
// שולח את כל הבתים ב-buf; חוזר רק כשהכל נשלח
void ServerClient::sendAll(int sock, const uint8_t* buf, size_t len) const
{
    size_t sent = 0;
    while (sent < len) {
        int n = ::send(sock,
                       reinterpret_cast<const char*>(buf + sent),
                       static_cast<int>(len - sent), 0);
        if (n <= 0)
            throw runtime_error("send() failed");
        sent += static_cast<size_t>(n);
    }
}

// ── recvAll ────────────────────────────────────────────────────────────────
// מקבל בדיוק len בתים; חוזר רק כשהכל התקבל
void ServerClient::recvAll(int sock, uint8_t* buf, size_t len) const
{
    size_t received = 0;
    while (received < len) {
        int n = ::recv(sock,
                       reinterpret_cast<char*>(buf + received),
                       static_cast<int>(len - received), 0);
        if (n <= 0)
            throw runtime_error("recv() failed or connection closed");
        received += static_cast<size_t>(n);
    }
}

} // namespace iris
