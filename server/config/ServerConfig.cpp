#include "ServerConfig.h"
#include <fstream>
#include <stdexcept>
#include <windows.h>
#include <nlohmann/json.hpp>
using namespace std;
// Convert UTF-8 → wide string using the Win32 API (handles all code points
// correctly; the `wstring(s.begin(), s.end())` trick only works for ASCII).
static std::wstring utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
    return out;
}

// הפונקציה קוראת את ההגדרות מקובץ json חיצוני 
//וממירה אותן לקובץ ServerConfig עם טיפוסי נתונים מתאימים לשימוש בשרת.
ServerConfig ServerConfig::loadFromFile(const string& jsonPath)
{
    std::ifstream file(jsonPath);// מנסה לפתוח את הקובץ בנתיב הנתון.
    if (!file.is_open())// אם לא מצליח לפתוח את הקובץ, זורק חריגה עם הודעה ברורה.
        throw runtime_error("ServerConfig: cannot open '" + jsonPath + "'");
    nlohmann::json j;//משתנה מסוג json של ספריית nlohmann, ישמש לקריאת הנתונים מהקובץ.
    file >> j;//שופך לתוך משתנה את כל מה שקרא מהקובץ וממיר אותו למבנה נתונים של json.

    ServerConfig cfg;//יוצר מופע של הגדרות ברירת מחדל, שיתעדכן עם הערכים מהקובץ.

    if (j.contains("port")) //אם הקובץ מכיל את השדה פורט מעדכן את הערך במבנה ההגדרות
       cfg.port = j["port"].get<int>();
    if (j.contains("numWorkers"))//אם הקובץ מכיל את השדה numWorkers מעדכן את הערך במבנה ההגדרות
       cfg.numWorkers = j["numWorkers"].get<int>();
    if (j.contains("hammingThreshold"))//אם הקובץ מכיל את השדה hammingThreshold מעדכן את הערך במבנה ההגדרות
       cfg.hammingThreshold = j["hammingThreshold"].get<double>();
    if (j.contains("normalizedWidth"))//אם הקובץ מכיל את השדה normalizedWidth מעדכן את הערך במבנה ההגדרות
       cfg.normalizedWidth = j["normalizedWidth"].get<int>();
    if (j.contains("normalizedHeight"))//אם הקובץ מכיל את השדה normalizedHeight מעדכן את הערך במבנה ההגדרות
       cfg.normalizedHeight = j["normalizedHeight"].get<int>();
    if (j.contains("dbConnectionString"))//אם הקובץ מכיל את השדה dbConnectionString מעדכן את הערך במבנה ההגדרות
       cfg.dbConnectionString = utf8ToWide(j["dbConnectionString"].get<string>());
    if (j.contains("allowedIPs") && j["allowedIPs"].is_array()) //אם הקובץ מכיל את השדה allowedIPs והוא מערך, מעדכן את הערך במבנה ההגדרות
    {
        cfg.allowedIPs.clear();//מנק מכל כתובות שאולי היו פעם
        for (const auto& ip : j["allowedIPs"])//רץ על רשימה ומכניס כתובות אח אחת לקובץ הגדרות
            cfg.allowedIPs.push_back(ip.get<string>());
    }
    return cfg;//מחזיר את קובץ הגדרות
}
