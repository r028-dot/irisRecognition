#include "TcpServer.h"
#include "ConnectionHandler.h"
#include "../security/Encryptor.h"
#include "../security/ReplayGuard.h"
#include "../utils/Logger.h"
#include "../utils/ThreadPool.h"
#include <openssl/err.h>
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "Ws2_32.lib")
// שרת TCP מאובטח (TLS) המנהל את חיבורי השערים במקביל (ThreadPool), מגן מפני התקפות DoS ומנתב בקשות ל-BiometricService.

namespace {
// קובעים את מספר ה-workers ב-ThreadPool לפי הקונפיגורציה, או לפי מספר הליבות (עם מינימום 4)
int resolveWorkerCount(int configuredWorkers)
{
    if (configuredWorkers > 0)
        return configuredWorkers;

    const unsigned int detected = std::thread::hardware_concurrency();
    return static_cast<int>(max(4u, detected == 0 ? 8u : detected));
}

// מחזיר את ערך משתנה הסביבה או ברירת מחדל אם הוא חסר או ריק
string envOrDefault(const char* envName, const char* fallback)
{
    const char* v = getenv(envName);
    return (v && *v) ? string(v) : string(fallback);
}
} // namespace


//בנאי השרת (Constructor): מאתחל את רשת הווינדוס, מקים ומגדיר את פורט ההאזנה (Socket) וטוען את מפתחות האבטחה (TLS).
TcpServer::TcpServer(shared_ptr<BiometricService> service, int port, int numWorkers,
                     vector<string> allowedIPs)
    : m_processor(std::move(service))
    , m_port(port)
    , m_numWorkers(resolveWorkerCount(numWorkers))
    , m_allowedIPs(allowedIPs.begin(), allowedIPs.end())
{
    // אתחול Winsock והכנת Socket להאזנה על הפורט המבוקש. אם יש שגיאה באחד השלבים, זורקים חריגה עם הודעה ברורה.
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        throw runtime_error("TcpServer: WSAStartup failed: "
                                 + to_string(WSAGetLastError()));

    m_listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET) {
        WSACleanup();
        throw runtime_error("TcpServer: socket() failed: "
                                 + to_string(WSAGetLastError()));
    }

    // מאפשר שימוש חוזר בכתובת (SO_REUSEADDR) כדי להקל על הפיתוח וההרצה החוזרת של השרת ללא צורך בהמתנה ל-timewait.
    int optVal = 1;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&optVal), sizeof(optVal));
    //פרטים על השרת
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<u_short>(m_port));
    //שיוך הכתובת והפורט לסוקט ומתחיל להאזין. אם יש שגיאה, סוגר את הסוקט, מנקה את Winsock, וזורק חריגה עם הודעה ברורה.
    if (::bind(m_listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) 
    {
        closesocket(m_listenSocket);
        WSACleanup();
        throw runtime_error("TcpServer: bind() failed on port "
                                 + to_string(m_port) + ": "
                                 + to_string(WSAGetLastError()));
    }
    // מאזין לחיבורים נכנסים. אם יש שגיאה, סוגר את הסוקט, מנקה את Winsock, וזורק חריגה עם הודעה ברורה.
    if (::listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(m_listenSocket);
        WSACleanup();
        throw runtime_error("TcpServer: listen() failed: "
                                 + to_string(WSAGetLastError()));
    }

    // אתחול TLS — טוען תעודה ומפתח פרטי.
    initTLS();
}

//הגדרת אבטחת השרת: יצירת הקשר TLS, חסימת פרוטוקולים ישנים ואימות התאמה בין תעודת הדיגיטלית למפתח הפרטי.
void TcpServer::initTLS()
{
    m_sslCtx = SSL_CTX_new(TLS_server_method());//אובייקט SSL_CTX מייצג את ההגדרות והמצב של TLS עבור השרת. הוא משמש ליצירת חיבורים מוצפנים חדשים.
    if (!m_sslCtx)
        throw runtime_error("TcpServer: SSL_CTX_new failed");

    // דורש TLS 1.2 ומעלה — חוסם פרוטוקולים ישנים ופגיעים 
    SSL_CTX_set_min_proto_version(m_sslCtx, TLS1_2_VERSION);

    const string certPath = envOrDefault("IRIS_TLS_CERT", "server.crt");
    const string keyPath  = envOrDefault("IRIS_TLS_KEY",  "server.key");
    //בודק אם ניתן לטעון את תעודת השרת ואת המפתח הפרטי. אם יש בעיה, משחרר את ה-SSL_CTX וזורק חריגה עם הודעה ברורה.
    if (SSL_CTX_use_certificate_file(m_sslCtx, certPath.c_str(),
                                     SSL_FILETYPE_PEM) <= 0) 
{
        SSL_CTX_free(m_sslCtx);
        m_sslCtx = nullptr;
        throw runtime_error("TcpServer: failed to load TLS certificate: "
                                 + certPath);
    }
    if (SSL_CTX_use_PrivateKey_file(m_sslCtx, keyPath.c_str(),
                                    SSL_FILETYPE_PEM) <= 0) {
        SSL_CTX_free(m_sslCtx);
        m_sslCtx = nullptr;
        throw runtime_error("TcpServer: failed to load TLS private key: "
                                 + keyPath);
    }
    // בודק אם המפתח הפרטי תואם לתעודה. אם לא, משחרר את ה-SSL_CTX וזורק חריגה עם הודעה ברורה.
    if (SSL_CTX_check_private_key(m_sslCtx) != 1) {
        SSL_CTX_free(m_sslCtx);
        m_sslCtx = nullptr;
        throw runtime_error("TcpServer: TLS private key does not match certificate");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Destructor
// ─────────────────────────────────────────────────────────────────────────────
TcpServer::~TcpServer()
{
    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }
    if (m_sslCtx) {
        SSL_CTX_free(m_sslCtx);
        m_sslCtx = nullptr;
    }
    WSACleanup();
}

// ─────────────────────────────────────────────────────────────────────────────
// isAllowedIP — בודק אם ה-IP נמצא ב-whitelist.
// אם ה-whitelist ריק — כל IP מותר (מצב פיתוח).
// ─────────────────────────────────────────────────────────────────────────────
bool TcpServer::isAllowedIP(const std::string& ip)
{
    if (m_allowedIPs.empty()) return true;
    return m_allowedIPs.count(ip) > 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// isRateLimited — בודק אם ה-IP חרג ממגבלת MAX_RATE בקשות בשנייה.
// ─────────────────────────────────────────────────────────────────────────────
bool TcpServer::isRateLimited(const std::string& ip)
{
    auto now = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    std::lock_guard<std::mutex> lock(m_rateMutex);
    auto& entry = m_rateLimiter[ip];

    if (entry.windowStart != now) {
        // חלון שנייה חדש — אפס מונה
        entry.windowStart = now;
        entry.count       = 0;
    }

    ++entry.count;
    return entry.count > MAX_RATE;
}

//מקבל חיבורים ומפנה ל-ThreadPool
void TcpServer::run()
{
    ThreadPool pool(static_cast<size_t>(m_numWorkers));

    Logger::instance().info("TcpServer: listening on port "
                            + std::to_string(m_port)
                            + " with " + std::to_string(m_numWorkers)
                            + " worker threads (TLS enabled)");
    if (!m_allowedIPs.empty())
        Logger::instance().info("TcpServer: IP whitelist active ("
                                + std::to_string(m_allowedIPs.size()) + " IPs)");
    // לולאת קבלה אינסופית של חיבורים נכנסים. כל חיבור עובר בדיקות IP ו-Rate Limit, ואז מועבר ל-ThreadPool לטיפול.
    for (;;) {
        sockaddr_in clientAddr{};
        int addrLen   = static_cast<int>(sizeof(clientAddr));
        SOCKET clientSock = ::accept(
            m_listenSocket,
            reinterpret_cast<sockaddr*>(&clientAddr),
            &addrLen);
        // אם יש שגיאה ב-accept, בודקים אם היא נובעת מסגירת השרת (WSAEINTR או WSAENOTSOCK) ומפסיקים את הלולאה. אחרת, רושמים אזהרה וממשיכים לקבל חיבורים.
        if (clientSock == INVALID_SOCKET) {
            int err = WSAGetLastError();
            if (err == WSAEINTR || err == WSAENOTSOCK)
                break;  // השרת נסגר בזמן המתנה ל-accept, יוצא מהלולאה
            Logger::instance().warning("TcpServer: accept() error: "
                                       + std::to_string(err));
            continue;
        }

        // קרא את ה-IP של הלקוח
        char ipStr[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
        string clientIP(ipStr);

        // בדיקה האם IP מותר
        if (!isAllowedIP(clientIP)) {
            Logger::instance().warning("TcpServer: rejected unlisted IP " + clientIP);
            closesocket(clientSock);
            continue;
        }

        // בדיקת Rate Limit (מניעת DDoS) 
        if (isRateLimited(clientIP)) {
            Logger::instance().warning("TcpServer: rate-limited IP " + clientIP);
            closesocket(clientSock);
            continue;
        }

        Logger::instance().info("TcpServer: connection from " + clientIP);
    
        int noDelay = 1;
        setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

        // שיוך ה-Socket לאובייקט SSL והעברת הטיפול בחיבור (הלחיצת יד והאימות) למאגר התהליכונים.
        SSL* ssl = SSL_new(m_sslCtx);
        if (!ssl) {
            Logger::instance().warning("TcpServer: SSL_new failed, dropping connection");
            closesocket(clientSock);
            continue;
        }
        SSL_set_fd(ssl, static_cast<int>(clientSock));

        // העבר את ה-SSL+socket ל-worker thread, עם הפניה למגן ה-Replay המשותף
        pool.enqueue([ssl,
                      sock = clientSock,
                      proc = m_processor,
                      &enc = m_encryptor,
                      &guard = m_replayGuard]() mutable
        {
            ConnectionHandler handler(ssl, sock, proc, enc, guard);
            handler.handle();
        });
    }
}
