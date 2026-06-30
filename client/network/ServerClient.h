#pragma once
#include <openssl/ssl.h>
#include <vector>
#include <cstdint>
#include <string>
#include "../config/ClientConfig.h"
using namespace std;
namespace iris {
//סטטוס אימות ביומטרי: AUTHORIZED = הצלחה, DENIED = נכשל, COMM_ERROR = בעיית תקשורת עם השרת.
enum class AuthStatus { AUTHORIZED, DENIED, COMM_ERROR };

struct AuthResponse {
    AuthStatus  status        = AuthStatus::COMM_ERROR;
    double hammingDist   = 1.0;
    int32_t  matchedUserID = -1;
    string matchedName;
    string flightNumber;
    string seatNumber;
    string message;
};

// תוצאה של בקשת הרשמה.
struct EnrollResult {
    bool        success   = false;
    int32_t     newUserID = -1;
    string message;
};

// מנהל החלפת TCP חד-פעמית עם שרת זיהוי הקשתית.
// כל קריאה ל-verify() פותחת חיבור חדש (מתאים ללולאת הקבלה של השרת).
class ServerClient {
public:
    explicit ServerClient(const ClientConfig& config);
    ~ServerClient();

    // אימות dual-eye: 3 תמונות שמאל + 3 תמונות ימין, השרת מחשב ממוצע HD
    AuthResponse verify(const string& passengerID,
                        const string& gateName,
                        const uint8_t iv[16],
                        const vector<vector<uint8_t>>& encryptedLeft,
                        const vector<vector<uint8_t>>& encryptedRight) const;
    // רישום נוסע חדש: שולח תמונות שמאל וימין, השרת מחלץ תבניות ביומטריות ושומר ב-DB
    EnrollResult enroll(const string& passengerID,
                        const string& fullName,
                        const string& nationality,
                        const uint8_t iv[16],
                        const vector<vector<uint8_t>>& encryptedLeft,
                        const vector<vector<uint8_t>>& encryptedRight) const;



private:
    SSL* connectToServer() const;
    void sendAll(SSL* ssl, const void* buf, std::size_t len) const;
    void recvAll(SSL* ssl, void*       buf, std::size_t len) const;

    SSL_CTX* m_ctx = nullptr;   // הקשר TLS של הלקוח (מאמת את תעודת השרת)
    string m_host;
    int m_port;
};

} 
