#pragma once
#include <string>
#include <fstream>
#include <mutex>
using namespace std;

// AccessLogger — כותב לקבצי CSV:
//   access_log.csv   : כל ניסיון מעבר בשער (מי, מתי, תוצאה, HD)
//   changes_log.csv  : כל שינוי במסד הנתונים (רישום, מחיקה וכו')
// המנהל יוכל לפתוח קבצים אלה ישירות.
class AccessLogger {
public:
    static AccessLogger& instance();

    void init(const string& logDir = ".");

    // רישום ניסיון מעבר בשער
    void logAccess(const string& passengerID,
                   const string& gate,
                   int eye,
                   double hammingDist,
                   bool success,
                   const string& notes = "");

   // רישום שינוי בקובץ לוג (רישום משתמש חדש, מחיקה, עדכון וכו')
    void logChange(const string& action,
                   const string& passengerID,
                   const string& details = "");

private:
    AccessLogger()  = default;
    ~AccessLogger();
    AccessLogger(const AccessLogger&) = delete;
    AccessLogger& operator=(const AccessLogger&) = delete;

    void writeHeader(std::ofstream& f, const string& header);
    static string now(); // timestamp נוכחי
    static string eyeName(int e); // "Left"/"Right"/"Dual"

    ofstream m_accessFile;
    ofstream m_changesFile;
    mutex    m_mutex;
};
