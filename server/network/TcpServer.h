#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <openssl/ssl.h>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <string>
#include <cstdint>
#include "../service/BiometricService.h"
#include "../security/ReplayGuard.h"
#include "../security/Encryptor.h"
using namespace std;

// שרת טי סי פי מאזין לחיבורי רשת מוצפנים ומעביר את הבקשות לשירות הביומטרי
class TcpServer {
public:
    TcpServer(shared_ptr<BiometricService> service, int port,int numWorkers = 8,
              vector<string> allowedIPs = {});
    ~TcpServer();
    void run();  // חוסם עד לעצירה — מקבל חיבורים ומפנה ל-ThreadPool
private:
    shared_ptr<BiometricService> m_processor;  // שירות ביומטרי שמטפל בבקשות IRIS
    int m_port;
    int m_numWorkers;
    SOCKET m_listenSocket = INVALID_SOCKET;
    SSL_CTX* m_sslCtx = nullptr;
    void initTLS();   // יוצר context וטוען cert+key
    Encryptor m_encryptor;
    std::unordered_set<std::string> m_allowedIPs;
    static constexpr int MAX_RATE = 5;
    struct RateLimitEntry { int count; int64_t windowStart; };
    std::unordered_map<std::string, RateLimitEntry> m_rateLimiter;
    std::mutex m_rateMutex;
    ReplayGuard m_replayGuard;
    bool isAllowedIP  (const std::string& ip);   
    bool isRateLimited(const std::string& ip);   
};
