#pragma once
#include <string>
#include <fstream>
#include <mutex>
using namespace std;
// מחלקה לוגים פשוטה שמדפיסה למסוף ולקובץ. תומכת ברמות לוגים שונות.
enum class LogLevel { LVL_DEBUG, LVL_INFO, LVL_WARNING, LVL_ERROR };

class Logger {
public:
    static Logger& instance();// מחזירה את מופע הסינגלטון של ה-Logger.
    void init(const string& filePath = "iris_server.log");// פותח (או יוצר) את קובץ הלוג. יש לקרוא פעם אחת בהפעלה.
    // פונקציות נוחות לרמות לוג שונות. כל אחת קוראת ל-log עם הרמה המתאימה.
    void debug  (const string& msg) { log(LogLevel::LVL_DEBUG,   msg); }
    void info   (const string& msg) { log(LogLevel::LVL_INFO,    msg); }
    void warning(const string& msg) { log(LogLevel::LVL_WARNING, msg); }
    void error  (const string& msg) { log(LogLevel::LVL_ERROR,   msg); }

    void log(LogLevel level, const string& msg);// כותב שורה עם תאריך/שעה ורמת לוג למסוף ולקובץ (אם פתוח).

private:
    Logger()  = default;// בונה פרטי כדי למנוע יצירת מופעים מחוץ למחלקה.
    ~Logger();// סוגר את קובץ הלוג אם הוא פתוח.
    Logger(const Logger&) = delete;// מונע העתקה של ה-Logger (סינגלטון).
    Logger& operator=(const Logger&) = delete;// מונע השמה של ה-Logger (סינגלטון).

    ofstream m_file;// אובייקט קובץ לכתיבה ללוג.
    mutex    m_mutex;// מגן על הגישה לקובץ הלוג כדי למנוע בעיות במקרים של ריבוי תהליכים.

    static string levelStr(LogLevel level);// מחזירה מחרוזת עם שם רמת הלוג.
    static string timestamp();// מחזירה מחרוזת עם התאריך והשעה הנוכחיים בפורמט "YYYY-MM-DD HH:MM:SS.mmm".
};
