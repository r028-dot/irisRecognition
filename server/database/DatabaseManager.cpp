// ============================================================
//  DatabaseManager.cpp  –  ODBC implementation for SQL Server
// ============================================================
#include "DatabaseManager.h"
#include <stdexcept>
#include <cstring>

// ---------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------
DatabaseManager::DatabaseManager(const std::wstring& connStr)
{
    checkRC(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_hEnv),
            m_hEnv, SQL_HANDLE_ENV, "Alloc ENV");

    checkRC(SQLSetEnvAttr(m_hEnv, SQL_ATTR_ODBC_VERSION,
                          reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0),
            m_hEnv, SQL_HANDLE_ENV, "Set ODBC version");

    checkRC(SQLAllocHandle(SQL_HANDLE_DBC, m_hEnv, &m_hDbc),
            m_hEnv, SQL_HANDLE_ENV, "Alloc DBC");

    SQLWCHAR    outStr[1024] = {};
    SQLSMALLINT outLen       = 0;
    SQLRETURN   rc = SQLDriverConnectW(
        m_hDbc, nullptr,
        const_cast<SQLWCHAR*>(connStr.c_str()), SQL_NTS,
        outStr, 1024, &outLen, SQL_DRIVER_NOPROMPT);

    checkRC(rc, m_hDbc, SQL_HANDLE_DBC, "Connect to SQL Server");
}

DatabaseManager::~DatabaseManager()
{
    if (m_hDbc != SQL_NULL_HDBC) {
        SQLDisconnect(m_hDbc);
        SQLFreeHandle(SQL_HANDLE_DBC, m_hDbc);
    }
    if (m_hEnv != SQL_NULL_HENV) {
        SQLFreeHandle(SQL_HANDLE_ENV, m_hEnv);
    }
}

// ---------------------------------------------------------------
// enrollUser
// ---------------------------------------------------------------
int DatabaseManager::enrollUser(const std::string& passportNumber,
                                const std::string& fullName,
                                const std::string& nationality,
                                const IrisCode& irisLeft,
                                const IrisCode& irisRight)
{
    SQLHSTMT hStmt = allocStmt();

    const wchar_t* sql =
        L"EXEC sp_EnrollUser "
        L"@PassportNumber=?, @FullName=?, @Nationality=?, "
        L"@IrisCodeLeft=?, @IrisCodeRight=?, @NewUserID=? OUTPUT";

    checkRC(SQLPrepareW(hStmt, const_cast<SQLWCHAR*>(sql), SQL_NTS),
            hStmt, SQL_HANDLE_STMT, "Prepare sp_EnrollUser");

    std::wstring wPassport    = strToWide(passportNumber);
    SQLLEN       passLen      = SQL_NTS;
    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     20, 0, const_cast<SQLWCHAR*>(wPassport.c_str()), 0, &passLen);

    std::wstring wFullName    = strToWide(fullName);
    SQLLEN       nameLen      = SQL_NTS;
    SQLBindParameter(hStmt, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     100, 0, const_cast<SQLWCHAR*>(wFullName.c_str()), 0, &nameLen);

    std::wstring wNationality = strToWide(nationality);
    SQLLEN       natLen       = SQL_NTS;
    SQLBindParameter(hStmt, 3, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     50, 0, const_cast<SQLWCHAR*>(wNationality.c_str()), 0, &natLen);

    auto   leftBytes = irisLeft.toBytes();
    SQLLEN leftLen   = static_cast<SQLLEN>(leftBytes.size());
    SQLBindParameter(hStmt, 4, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_VARBINARY,
                     512, 0, leftBytes.data(), 512, &leftLen);

    auto   rightBytes = irisRight.toBytes();
    SQLLEN rightLen   = static_cast<SQLLEN>(rightBytes.size());
    SQLBindParameter(hStmt, 5, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_VARBINARY,
                     512, 0, rightBytes.data(), 512, &rightLen);

    SQLINTEGER newUserID    = 0;
    SQLLEN     newUserIDLen = sizeof(SQLINTEGER);
    SQLBindParameter(hStmt, 6, SQL_PARAM_OUTPUT, SQL_C_SLONG, SQL_INTEGER,
                     0, 0, &newUserID, sizeof(newUserID), &newUserIDLen);

    checkRC(SQLExecute(hStmt), hStmt, SQL_HANDLE_STMT, "Execute sp_EnrollUser");
    freeStmt(hStmt);
    return static_cast<int>(newUserID);
}

// ---------------------------------------------------------------
// getUserByPassport
// ---------------------------------------------------------------
std::optional<User> DatabaseManager::getUserByPassport(const std::string& passportNumber)
{
    SQLHSTMT hStmt = allocStmt();

    const wchar_t* sql = L"EXEC sp_GetUserByPassport @PassportNumber=?";
    checkRC(SQLPrepareW(hStmt, const_cast<SQLWCHAR*>(sql), SQL_NTS),
            hStmt, SQL_HANDLE_STMT, "Prepare sp_GetUserByPassport");

    std::wstring wPassport = strToWide(passportNumber);
    SQLLEN       passLen   = SQL_NTS;
    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     20, 0, const_cast<SQLWCHAR*>(wPassport.c_str()), 0, &passLen);

    checkRC(SQLExecute(hStmt), hStmt, SQL_HANDLE_STMT, "Execute sp_GetUserByPassport");

    // SP returns: UserID, PassportNumber, FullName, Nationality, Eye, IrisCodeData
    // Up to 2 rows (one per registered eye)
    User user;
    bool found = false;

    while (SQLFetch(hStmt) == SQL_SUCCESS) {
        SQLLEN ind = 0;

        SQLINTEGER uid = 0;
        SQLGetData(hStmt, 1, SQL_C_SLONG, &uid, sizeof(uid), &ind);

        SQLWCHAR skipBuf[32] = {};
        SQLGetData(hStmt, 2, SQL_C_WCHAR, skipBuf, sizeof(skipBuf), &ind);

        SQLWCHAR wName[200] = {};
        SQLGetData(hStmt, 3, SQL_C_WCHAR, wName, sizeof(wName), &ind);

        SQLWCHAR wNat[100] = {};
        SQLGetData(hStmt, 4, SQL_C_WCHAR, wNat, sizeof(wNat), &ind);

        if (!found) {
            user.userID         = static_cast<int>(uid);
            user.passportNumber = passportNumber;
            user.fullName       = wstrToUtf8(wName);
            user.nationality    = wstrToUtf8(wNat);
            found = true;
        }

        SQLSMALLINT eyeVal = 0;
        SQLGetData(hStmt, 5, SQL_C_SSHORT, &eyeVal, sizeof(eyeVal), &ind);

        uint8_t  irisBytes[512] = {};
        SQLLEN   irisLen        = 0;
        SQLRETURN rc = SQLGetData(hStmt, 6, SQL_C_BINARY, irisBytes, 512, &irisLen);
        if ((rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) && irisLen == 512) {
            IrisCode code = IrisCode::fromBytes(irisBytes, 512);
            if (eyeVal == 0) { user.irisCodeLeft  = code; user.hasLeft  = true; }
            else             { user.irisCodeRight = code; user.hasRight = true; }
        }
    }

    freeStmt(hStmt);
    if (!found) return std::nullopt;
    return user;
}

// ---------------------------------------------------------------
// userExists
// ---------------------------------------------------------------
bool DatabaseManager::userExists(const std::string& passportNumber)
{
    SQLHSTMT hStmt = allocStmt();

    const wchar_t* sql =
        L"SELECT COUNT(1) FROM Users WHERE PassportNumber=? AND IsActive=1";
    checkRC(SQLPrepareW(hStmt, const_cast<SQLWCHAR*>(sql), SQL_NTS),
            hStmt, SQL_HANDLE_STMT, "Prepare userExists");

    std::wstring wPassport = strToWide(passportNumber);
    SQLLEN       passLen   = SQL_NTS;
    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     20, 0, const_cast<SQLWCHAR*>(wPassport.c_str()), 0, &passLen);

    checkRC(SQLExecute(hStmt), hStmt, SQL_HANDLE_STMT, "Execute userExists");

    SQLINTEGER count = 0;
    SQLLEN     ind   = 0;
    if (SQLFetch(hStmt) == SQL_SUCCESS)
        SQLGetData(hStmt, 1, SQL_C_SLONG, &count, sizeof(count), &ind);

    freeStmt(hStmt);
    return count > 0;
}

// ---------------------------------------------------------------
// logAuthAttempt
// ---------------------------------------------------------------
void DatabaseManager::logAuthAttempt(int userID, int eye, bool success,
                                     double hammingDist, const std::string& notes)
{
    SQLHSTMT hStmt = allocStmt();

    const wchar_t* sql =
        L"EXEC sp_LogAuthAttempt "
        L"@MatchedUserID=?, @Eye=?, @Success=?, @HammingDist=?, @Notes=?";
    checkRC(SQLPrepareW(hStmt, const_cast<SQLWCHAR*>(sql), SQL_NTS),
            hStmt, SQL_HANDLE_STMT, "Prepare sp_LogAuthAttempt");

    SQLLEN     uid_ind = 0;
    SQLINTEGER uid     = userID;
    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
                     0, 0, &uid, 0, &uid_ind);

    SQLLEN      eye_ind = 0;
    SQLSMALLINT eyeVal  = static_cast<SQLSMALLINT>(eye);
    SQLBindParameter(hStmt, 2, SQL_PARAM_INPUT, SQL_C_SSHORT, SQL_TINYINT,
                     0, 0, &eyeVal, 0, &eye_ind);

    SQLLEN      succ_ind = 0;
    SQLSMALLINT succVal  = success ? 1 : 0;
    SQLBindParameter(hStmt, 3, SQL_PARAM_INPUT, SQL_C_SSHORT, SQL_BIT,
                     0, 0, &succVal, 0, &succ_ind);

    SQLLEN    dist_ind = 0;
    SQLDOUBLE dist     = hammingDist;
    SQLBindParameter(hStmt, 4, SQL_PARAM_INPUT, SQL_C_DOUBLE, SQL_FLOAT,
                     0, 0, &dist, 0, &dist_ind);

    std::wstring wNotes   = strToWide(notes);
    SQLLEN       notesLen = notes.empty() ? SQL_NULL_DATA : SQL_NTS;
    SQLBindParameter(hStmt, 5, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     255, 0, const_cast<SQLWCHAR*>(wNotes.c_str()), 0, &notesLen);

    checkRC(SQLExecute(hStmt), hStmt, SQL_HANDLE_STMT, "Execute sp_LogAuthAttempt");
    freeStmt(hStmt);
}

// ---------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------
SQLHSTMT DatabaseManager::allocStmt() const
{
    SQLHSTMT hStmt = SQL_NULL_HSTMT;
    checkRC(SQLAllocHandle(SQL_HANDLE_STMT, m_hDbc, &hStmt),
            m_hDbc, SQL_HANDLE_DBC, "Alloc STMT");
    return hStmt;
}

void DatabaseManager::freeStmt(SQLHSTMT hStmt)
{
    if (hStmt != SQL_NULL_HSTMT)
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
}

void DatabaseManager::checkRC(SQLRETURN rc, SQLHANDLE handle,
                               SQLSMALLINT handleType, const char* context)
{
    if (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO)
        return;

    SQLWCHAR    state[6]    = {};
    SQLWCHAR    msg[1024]   = {};
    SQLINTEGER  nativeErr   = 0;
    SQLSMALLINT msgLen      = 0;
    SQLGetDiagRecW(handleType, handle, 1, state, &nativeErr, msg, 1024, &msgLen);

    char narrow[1024] = {};
    WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<wchar_t*>(msg), -1,
                        narrow, sizeof(narrow), nullptr, nullptr);
    throw std::runtime_error(std::string(context) + ": " + narrow);
}

std::string DatabaseManager::wstrToUtf8(const SQLWCHAR* wstr)
{
    if (!wstr || wstr[0] == 0) return {};
    const wchar_t* p = reinterpret_cast<const wchar_t*>(wstr);
    int len = WideCharToMultiByte(CP_UTF8, 0, p, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, p, -1, result.data(), len, nullptr, nullptr);
    return result;
}

std::wstring DatabaseManager::strToWide(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring result(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, result.data(), len);
    return result;
}
