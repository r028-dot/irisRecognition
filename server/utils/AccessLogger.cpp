#include "AccessLogger.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
using namespace std;
namespace fs = filesystem;

//יוצר מופע יחיד (Singleton) של AccessLogger ומחזיר אליו הפניה
AccessLogger& AccessLogger::instance()
{
    static AccessLogger inst;
    return inst;
}

// Destructor: סוגר את קובצי הלוג אם הם פתוחים
AccessLogger::~AccessLogger()
{
    if (m_accessFile.is_open())  m_accessFile.close();
    if (m_changesFile.is_open()) m_changesFile.close();
}

//מאתחל את ה Logger ופותח את קובצי הלוג
void AccessLogger::init(const string& logDir)
{
    lock_guard<mutex> lock(m_mutex);

    fs::create_directories(logDir);

    // access_log.csv — מעברים בשער
    string accessPath = logDir + "/access_log.csv";
    bool accessExists = fs::exists(accessPath);
    m_accessFile.open(accessPath, std::ios::app);
    if (!accessExists)
        writeHeader(m_accessFile,
                    "Timestamp,PassengerID,Gate,Eye,HammingDist,Success,Notes\n");

    // changes_log.csv — שינויים במסד הנתונים
    string changesPath = logDir + "/changes_log.csv";
    bool changesExists = fs::exists(changesPath);
    m_changesFile.open(changesPath, std::ios::app);
    if (!changesExists)
        writeHeader(m_changesFile,
                    "Timestamp,Action,PassengerID,Details\n");
}

//אחראי על רישום ניסיונות גישה בקובץ לוגים
void AccessLogger::logAccess(const string& passengerID,
                              const string& gate,
                              int eye,
                              double hammingDist,
                              bool success,
                              const string& notes)
{
    lock_guard<mutex> lock(m_mutex);
    if (!m_accessFile.is_open()) return;
    // פורמט: Timestamp,PassengerID,Gate,Eye,HammingDist,Success,Notes
    m_accessFile << now() << ','
                 << passengerID << ','
                 << gate << ','
                 << eyeName(eye) << ','
                 << std::fixed << std::setprecision(6) << hammingDist << ','
                 << (success ? "PASS" : "FAIL") << ','
                 << notes << '\n';
    m_accessFile.flush();
}

//אחראי על רישום שינויים בקובץ לוגים
void AccessLogger::logChange(const string& action,
                              const string& passengerID,
                              const string& details)
{
    lock_guard<mutex> lock(m_mutex);
    if (!m_changesFile.is_open()) return;

    m_changesFile << now() << ','
                  << action << ','
                  << passengerID << ','
                  << details << '\n';
    m_changesFile.flush();
}

// פונקציה פנימית לכתיבת כותרת לקובץ לוג
void AccessLogger::writeHeader(std::ofstream& f, const string& header)
{
    f << header;
    f.flush();
}

//פונקציה פנימית שמחזירה את התאריך והשעה הנוכחיים בפורמט "YYYY-MM-DD HH:MM:SS.mmm"
string AccessLogger::now()
{
    using namespace std::chrono;
    auto tp  = system_clock::now();
    auto tt  = system_clock::to_time_t(tp);
    auto ms  = duration_cast<milliseconds>(tp.time_since_epoch()) % 1000;
    std::tm buf{};
#ifdef _WIN32
    localtime_s(&buf, &tt);
#else
    localtime_r(&tt, &buf);
#endif
    std::ostringstream ss;
    ss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

//פונקציה פנימית המחזירה את שם העין לפי מספרה
string AccessLogger::eyeName(int e)
{
    switch (e) {
        case 0:  return "Left";
        case 1:  return "Right";
        case 2:  return "Dual";
        default: return "Unknown";
    }
}
