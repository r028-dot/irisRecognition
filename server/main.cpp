#include <iostream>//מייבא את ספריית הקלט/פלט של C++.
#include <memory>//מייבא את ספריית הזיכרון החכם של C++.
#include <filesystem>//מייבא את ספריית מערכת הקבצים של C++.
#include <cstdlib>//מייבא את ספריית הפונקציות הכלליות של C++.
#include "config/ServerConfig.h"//מייבא את הגדרת מבנה התצורה וטעינתו מקובץ.
#include "database/DatabaseManager.h"//מייבא את מנהל בסיס הנתונים.
#include "service/BiometricService.h"//מייבא את שירות הביומטריה.
#include "network/TcpServer.h"//מייבא את שרת ה-TCP.
#include "utils/Logger.h"//מייבא את מערכת הלוגים.
#include "utils/AccessLogger.h"//מייבא את לוגר המעברים והשינויים לקבצי CSV.
using namespace std;
namespace fs = std::filesystem;//נותן קיצור נוח לשימוש בספריית מערכת הקבצים.

// בודק שמשתנה הסביבה קיים ואינו ריק. זורק חריגה עם הודעה ברורה אחרת.
static void requireEnvVar(const char* name)
{
    const char* val = getenv(name);// מחפש את המשתנה בסביבה.
    if (!val || val[0] == '\0')
        throw runtime_error(string("משתנה סביבה חסר: ") + name +" — הגדר אותו לפני הפעלת השרת.");
}

// בודק שקובץ TLS קיים (לפי משתנה סביבה או ברירת מחדל).
static void requireFile(const char* envVar, const char* fallback, const char* desc)//מקבל שם משתנה סביבה, נתיב ברירת מחדל, ותיאור (לשגיאה).
{
    const char* v = getenv(envVar);// מחפש את המשתנה בסביבה.
    const string path = (v && *v) ? string(v) : string(fallback);//אם המשתנה קיים ואינו ריק, משתמש בו, אחרת משתמש בנתיב ברירת המחדל.
    if (!fs::exists(path))//בודק אם הקובץ לא קיים במערכת הקבצים.
        throw runtime_error(string(desc) + " לא נמצא: " + path +" (הגדר " + envVar + " או הנח את הקובץ בתיקיית העבודה).");//זורק חריגה עם הודעה ברורה אם הקובץ לא נמצא.
}

// בודק פרמטרי config בסיסיים.
static void validateConfig(const ServerConfig& cfg)
{
    if (cfg.port < 1 || cfg.port > 65535)
        throw runtime_error("port לא תקין: " + std::to_string(cfg.port));
    if (cfg.numWorkers <= 0)
        throw runtime_error("numWorkers חייב להיות גדול מ-0");
    if (cfg.hammingThreshold < 0.0 || cfg.hammingThreshold > 1.0)
        throw runtime_error("hammingThreshold חייב להיות בטווח [0,1]");
    if (cfg.dbConnectionString.empty())
        throw runtime_error("dbConnectionString ריק ב-config.json");
}

// מחפש את קובץ config.json במספר מיקומים אפשריים ומחזיר את הנתיב הראשון שמצא.
static string locateConfig()
{
    const char* candidates[] = {
        "config/config.json",
        "../config/config.json",
        "config.json"
    };
    for (const char* c : candidates) {
        if (fs::exists(c)) return c;
    }
    return "config/config.json"; // אם לא נמצא, מחזיר ברירת מחדל (הפעלה תיכשל בהמשך).
}

int main()
{
    Logger::instance().init("iris_server.log");// פותח את קובץ הלוג הראשי של השרת (או יוצר אותו אם לא קיים).
    AccessLogger::instance().init("logs");     // פותח access_log.csv ו-changes_log.csv בתיקיית logs/
    Logger::instance().info("=== Iris Recognition Server starting ===");// כותב שורת פתיחה ללוג הראשי של השרת.

    try {
        // שלב 1: בדיקות מוקדמות על משתני סביבה וקבצי TLS 
        requireEnvVar("IRIS_AES_KEY");      // מפתח הצפנת תקשורת
        requireEnvVar("IRIS_DB_AES_KEY");   // מפתח הצפנת בסיס הנתונים
        requireFile("IRIS_TLS_CERT", "server.crt", "תעודת TLS");// מוודא שקובץ תעודת TLS קיים (לפי משתנה סביבה או ברירת מחדל).
        requireFile("IRIS_TLS_KEY",  "server.key", "מפתח פרטי TLS");// מוודא שקובץ מפתח פרטי TLS קיים (לפי משתנה סביבה או ברירת מחדל).
        Logger::instance().info("בדיקות מוקדמות עברו בהצלחה (env vars + TLS files)");// כותב ללוג שהבדיקות המוקדמות עברו בהצלחה.

        // שלב 2: טעינת תצורה מקובץ JSON (עם בדיקת תקינות בסיסית)
        const string cfgPath = locateConfig();// מחפש את קובץ התצורה במספר מיקומים אפשריים ומחזיר את הנתיב הראשון שמצא.
        ServerConfig cfg = ServerConfig::loadFromFile(cfgPath);// טוען את התצורה מקובץ JSON ומחזיר אובייקט ServerConfig.
        validateConfig(cfg);// בודק את תקינות הפרמטרים הבסיסיים של התצורה (פורט, מספר עובדים, סף מרחק מינג, מחרוזת חיבור DB).
        Logger::instance().info("Config loaded from " + cfgPath
                                + " - port=" + to_string(cfg.port)
                                + " workers=" + to_string(cfg.numWorkers));

        // שלב 3: מייצר את שכב ת בסיס הנתונים — DatabaseManager מיישם IUserRepository
        auto db = std::make_shared<DatabaseManager>(cfg.dbConnectionString);// יוצר מופע של DatabaseManager עם מחרוזת החיבור לבסיס הנתונים.
        Logger::instance().info("Connected to IrisRecognitionDB");

        // שלב 4: מייצר את שירות הביומטריה — BiometricService משתמש ב-DatabaseManager ובאלגוריתם IRIS
        auto service = std::make_shared<BiometricService>(db, cfg.hammingThreshold);

        // שלב 5: הפעל שרת 
        Logger::instance().info("Listening on port " + to_string(cfg.port) + "...");
        TcpServer server(service, cfg.port, cfg.numWorkers, cfg.allowedIPs);// יוצר מופע של TcpServer עם שירות הביומטריה, פורט, מספר עובדים, ורשימת IPs מותרות.
        server.run();// מפעיל את שרת ה-TCP (חוסם עד לעצירה) — מקבל חיבורים ומפנה ל-ThreadPool.

    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Fatal: ") + e.what());
        return 1;
    }

    Logger::instance().info("Server stopped.");
    return 0;
}
