#include "ServerConfig.h"
#include <fstream>
#include <stdexcept>
#include <windows.h>
#include <nlohmann/json.hpp>
using namespace std;

// מפעילה את הקובץ ומעבירה טקסט בפורמט UTF-8 לUTF-16
static std::wstring utf8ToWide(const string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 1) return {};
    wstring out(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
    return out;
}

// הפונקציה קוראת את ההגדרות מקובץ json חיצוני 
//וממירה אותן לקובץ ServerConfig עם טיפוסי נתונים מתאימים לשימוש בשרת.
ServerConfig ServerConfig::loadFromFile(const string& jsonPath)
{
   // פותח את הקובץ לקריאה. אם לא מצליח, זורק חריגה עם הודעה ברורה.
    std::ifstream file(jsonPath);
    if (!file.is_open())
        throw runtime_error("ServerConfig: cannot open '" + jsonPath + "'");
    nlohmann::json j;
    file >> j;

    ServerConfig cfg;
   // אם קיימים ערכים בקובץ JSON, מעדכן את השדות המתאימים ב-ServerConfig. אם לא, נשארים הערכים כברירת מחדל.
    if (j.contains("port"))
       cfg.port = j["port"].get<int>();
    if (j.contains("numWorkers"))
       cfg.numWorkers = j["numWorkers"].get<int>();
    if (j.contains("hammingThreshold"))
       cfg.hammingThreshold = j["hammingThreshold"].get<double>();
    if (j.contains("normalizedWidth"))
       cfg.normalizedWidth = j["normalizedWidth"].get<int>();
    if (j.contains("normalizedHeight"))
       cfg.normalizedHeight = j["normalizedHeight"].get<int>();
    if (j.contains("maxVerifyImages"))
       cfg.maxVerifyImages = j["maxVerifyImages"].get<int>();
    if (j.contains("maxEnrollImages"))
       cfg.maxEnrollImages = j["maxEnrollImages"].get<int>();
    if (j.contains("minValidProbes"))
       cfg.minValidProbes = j["minValidProbes"].get<int>();
    if (j.contains("dbConnectionString"))
       cfg.dbConnectionString = utf8ToWide(j["dbConnectionString"].get<string>());
    if (j.contains("allowedIPs") && j["allowedIPs"].is_array())
    {
        cfg.allowedIPs.clear();
        for (const auto& ip : j["allowedIPs"])
            cfg.allowedIPs.push_back(ip.get<string>());
    }
    return cfg;
}
