#pragma once
#include <string>
#include <optional>
#include <vector>
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include "../models/User.h"
#include "../models/AuthResult.h"
#include "../security/Encryptor.h"   // הצפנת תבניות ביומטריות במנוחה
#include "IUserRepository.h"   // מכיל גם את הגדרת GateAccessResult
using namespace std;

// מימוש של IUserRepository שמתחבר למסד נתונים SQL Server באמצעות ODBC.
class DatabaseManager : public IUserRepository {
public:
    explicit DatabaseManager(const wstring& connectionString);// מחבר למסד הנתונים עם מחרוזת החיבור הנתונה. זורק חריגות אם החיבור נכשל.
    ~DatabaseManager();// סוגר את החיבור למסד הנתונים ומשחרר משאבים. זורק חריגות אם יש בעיה בשחרור המשאבים.
    DatabaseManager(const DatabaseManager&) = delete;// מונע העתקה של DatabaseManager (סינגלטון).
    DatabaseManager& operator=(const DatabaseManager&) = delete;// מונע השמה של DatabaseManager (סינגלטון).

    // מימוש IUserRepository 
   // רושם משתמש חדש עם תבניות ביומטריות. מחזיר UserID שהוקצה.
    int enrollUser(const string& passengerID,
                   const string& fullName,
                   const string& nationality,
                   const vector<IrisCode>& irisLeft,
                   const vector<IrisCode>& irisRight) override;
    
    // טוען משתמש + תבניות Iris לפי מזהה נוסע. מחזיר nullopt אם לא נמצא.
    std::optional<User> getUserByID(const string& passengerID) override;
    
    // מחזיר true אם מזהה הנוסע כבר קיים במסד הנתונים.
    bool userExists(const string& passengerID) override;

    // מחזיר את כל התבניות הביומטריות לנוסע ועין נתונים (0=שמאל, 1=ימין).
    vector<IrisCode> getAllIrisCodes(const string& passengerID, int eye) override;
    
    // בודק האם המשתמש המזהה מורשה לעבור בשער הנוכחי.
    GateAccessResult checkGateAccess(int userID, const string& gateName) override;

    // מתעד ניסיון אימות ב-RecognitionLog.
    void logAuthAttempt(int userID, int eye, bool success,
                        double hammingDist, const string& notes = "") override;

private:
    SQLHENV m_hEnv = SQL_NULL_HENV;//משתנה שמייצג את הסביבה של ODBC, משמש לניהול חיבורים ומשאבים. הוא מאותחל ב-NULL ומוקצה בקונסטרקטור.
    SQLHDBC m_hDbc = SQL_NULL_HDBC;//משתנה שמייצג את חיבור ODBC למסד הנתונים. הוא מאותחל ב-NULL ומוקצה בקונסטרקטור.

    // הצפנת תבניות IrisCode לפני שמירה במסד הנתונים —
    // מפתח נפרד מהצפנת הרשת (IRIS_DB_AES_KEY).
    // אם משתנה הסביבה לא קיים, DatabaseManager זורק חריגה בהפעלה.
    Encryptor m_dbEncryptor{ "IRIS_DB_AES_KEY" };

    SQLHSTMT allocStmt() const;// פונקציה עזר שמקצה ומחזירה סטייטמנט ODBC. זורקת חריגה אם ההקצאה נכשלת.
    static void freeStmt(SQLHSTMT hStmt);// פונקציה עזר שמפנה משאבים של סטייטמנט ODBC. זורקת חריגה אם יש בעיה בשחרור המשאבים.
    static void checkRC(SQLRETURN rc, SQLHANDLE handle,
                        SQLSMALLINT handleType, const char* context);// פונקציה עזר שמוודאת אם קוד החזרה ODBC מציין הצלחה או כישלון. אם יש שגיאה, היא אוספת את פרטי השגיאה ומעלה חריגה עם מידע זה.
    static string  wstrToUtf8(const SQLWCHAR* wstr);
    static wstring strToWide(const string& s);
};
