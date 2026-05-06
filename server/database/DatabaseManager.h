#pragma once
// ============================================================
//  DatabaseManager.h  –  SQL Server ODBC layer
// ============================================================
#include <string>
#include <optional>
#include <vector>
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include "../models/User.h"
#include "../models/AuthResult.h"

class DatabaseManager {
public:
    // connection string example:
    // L"DRIVER={ODBC Driver 17 for SQL Server};"
    // L"SERVER=.\\SQLEXPRESS;DATABASE=IrisRecognitionDB;Trusted_Connection=yes;"
    explicit DatabaseManager(const std::wstring& connectionString);
    ~DatabaseManager();

    DatabaseManager(const DatabaseManager&)            = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    // Enroll: save user + both iris codes (up to 3 templates per eye).
    // irisLeft[0] and irisRight[0] are mandatory; indices 1 and 2 are optional.
    // Returns assigned UserID.
    int enrollUser(const std::string& passportNumber,
                   const std::string& fullName,
                   const std::string& nationality,
                   const std::vector<IrisCode>& irisLeft,
                   const std::vector<IrisCode>& irisRight);

    // Load user + iris codes by passport number. Returns nullopt if not found.
    std::optional<User> getUserByPassport(const std::string& passportNumber);

    // Returns true if passport number is already in the DB.
    bool userExists(const std::string& passportNumber);

    // Returns all stored IrisCodes for a given passport + eye (0=Left, 1=Right).
    // Reads IrisCode1/2/3 from the single row; returns 1–3 entries.
    std::vector<IrisCode> getAllIrisCodes(const std::string& passportNumber, int eye);

    // Append one row to RecognitionLog.
    void logAuthAttempt(int userID, int eye, bool success,
                        double hammingDist, const std::string& notes = "");

private:
    SQLHENV m_hEnv = SQL_NULL_HENV;
    SQLHDBC m_hDbc = SQL_NULL_HDBC;

    SQLHSTMT allocStmt() const;
    static void freeStmt(SQLHSTMT hStmt);
    static void checkRC(SQLRETURN rc, SQLHANDLE handle,
                        SQLSMALLINT handleType, const char* context);
    static std::string  wstrToUtf8(const SQLWCHAR* wstr);
    static std::wstring strToWide(const std::string& s);
};
