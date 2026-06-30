#include "Logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
using namespace std;
Logger& Logger::instance()
{
    static Logger inst;
    return inst;
}

Logger::~Logger()
{
    if (m_file.is_open())
        m_file.close();
}

void Logger::init(const string& filePath)// פותח (או יוצר) את קובץ הלוג. יש לקרוא פעם אחת בהפעלה.
{
    lock_guard<mutex> lock(m_mutex);// מגן על הגישה לקובץ הלוג כדי למנוע בעיות במקרים של ריבוי תהליכים.
    if (m_file.is_open()) m_file.close();
    m_file.open(filePath, std::ios::app);
}

void Logger::log(LogLevel level, const string& msg)// כותב שורה עם תאריך/שעה ורמת לוג למסוף ולקובץ (אם פתוח).
{
    string line = "[" + timestamp() + "] [" + levelStr(level) + "] " + msg;
    lock_guard<mutex> lock(m_mutex);
    std::cout << line << '\n';
    if (m_file.is_open())
        m_file << line << '\n';
}

string Logger::levelStr(LogLevel level)// מחזירה מחרוזת עם שם רמת הלוג.
{
    switch (level) {
        case LogLevel::LVL_DEBUG:   return "DEBUG";
        case LogLevel::LVL_INFO:    return "INFO ";
        case LogLevel::LVL_WARNING: return "WARN ";
        case LogLevel::LVL_ERROR:   return "ERROR";
    }
    return "?????";
}

string Logger::timestamp()// מחזירה מחרוזת עם התאריך והשעה הנוכחיים בפורמט "YYYY-MM-DD HH:MM:SS.mmm".
{
    auto now   = chrono::system_clock::now();
    auto time  = chrono::system_clock::to_time_t(now);
    auto ms    = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()) % 1000;
    ostringstream ss;
    ss << put_time(localtime(&time), "%Y-%m-%d %H:%M:%S")
       << '.' << setfill('0') << setw(3) << ms.count();
    return ss.str();
}
