#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include "../config/ClientConfig.h"
using namespace std;

namespace iris {

// תוצאת בקשת זיהוי מהשרת
enum class AuthStatus { AUTHORIZED, DENIED, ERROR };

struct AuthResponse {
    AuthStatus  status   = AuthStatus::ERROR;
    string message;   // הודעה מהשרת (אופציונלי)
};

// מנהל את חיבור TCP לשרת: שליחה וקבלה
class ServerClient {
public:
    explicit ServerClient(const ClientConfig& config);
    ~ServerClient();

    // שולח תמונת קשתית מוצפנת, מחזיר תשובת זיהוי
    AuthResponse sendIrisImage(const vector<uint8_t>& encryptedImage) const;

private:
    // פותח שקע TCP לשרת, זורק חריג אם נכשל
    int  connectToServer() const;
    void sendAll(int sock, const uint8_t* buf, size_t len) const;
    void recvAll(int sock, uint8_t* buf, size_t len) const;

    string m_host;
    int m_port;
};

} // namespace iris
