#pragma once
#include <string>
#include <optional>
#include <vector>
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include "../models/User.h"
#include "../models/AuthResult.h"
#include "../security/Encryptor.h"  
#include "IUserRepository.h"   
using namespace std;

// מימוש של IUserRepository שמתחבר למסד נתונים SQL Server באמצעות ODBC.
class DatabaseManager : public IUserRepository {
public:
    explicit DatabaseManager(const wstring& connectionString);
    ~DatabaseManager();
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

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

    // בודק האם המשתמש המזהה מורשה לעבור בשער הנוכחי.
    GateAccessResult checkGateAccess(int userID, const string& gateName) override;

private:
    SQLHENV m_hEnv = SQL_NULL_HENV;
    SQLHDBC m_hDbc = SQL_NULL_HDBC;

    Encryptor m_dbEncryptor{ "IRIS_DB_AES_KEY" };

    //פונקציות עזר
    SQLHSTMT allocStmt() const;
    static void freeStmt(SQLHSTMT hStmt);
    static void checkRC(SQLRETURN rc, SQLHANDLE handle,
                        SQLSMALLINT handleType, const char* context);
    static string  wstrToUtf8(const SQLWCHAR* wstr);
    static wstring strToWide(const string& s);
};
