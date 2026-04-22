#pragma once
// ============================================================
//  IrisDB.h  -  SQL Server ODBC helper for Iris Recognition
//  Requires: Windows SDK, link with odbc32.lib
// ============================================================

#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <string>
#include <vector>
#include <stdexcept>

// ---------------------------------------------------------------
// Struct: IrisRecord
// Holds one row from IrisFeatures joined with Users
// ---------------------------------------------------------------
struct IrisRecord {
    int                  featureID  = 0;
    int                  userID     = 0;
    std::wstring         username;
    std::wstring         fullName;
    int                  eye        = 0;   // 0=Left, 1=Right
    std::vector<float>   featureVector;
};

// ---------------------------------------------------------------
// Class: IrisDB
// ---------------------------------------------------------------
class IrisDB {
public:
    // Connection string format:
    // L"DRIVER={ODBC Driver 17 for SQL Server};SERVER=.\\SQLEXPRESS;DATABASE=IrisRecognitionDB;Trusted_Connection=yes;"
    explicit IrisDB(const std::wstring& connectionString)
        : m_hEnv(SQL_NULL_HENV), m_hDbc(SQL_NULL_HDBC)
    {
        checkRC(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_hEnv),
                m_hEnv, SQL_HANDLE_ENV, "Alloc ENV");

        checkRC(SQLSetEnvAttr(m_hEnv, SQL_ATTR_ODBC_VERSION,
                              reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0),
                m_hEnv, SQL_HANDLE_ENV, "Set ODBC version");

        checkRC(SQLAllocHandle(SQL_HANDLE_DBC, m_hEnv, &m_hDbc),
                m_hEnv, SQL_HANDLE_ENV, "Alloc DBC");

        SQLWCHAR outStr[1024] = {};
        SQLSMALLINT outLen = 0;
        SQLRETURN rc = SQLDriverConnectW(
            m_hDbc,
            nullptr,
            const_cast<SQLWCHAR*>(connectionString.c_str()),
            SQL_NTS,
            outStr, static_cast<SQLSMALLINT>(sizeof(outStr) / sizeof(SQLWCHAR)),
            &outLen,
            SQL_DRIVER_NOPROMPT);

        checkRC(rc, m_hDbc, SQL_HANDLE_DBC, "Connect");
    }

    ~IrisDB() {
        if (m_hDbc != SQL_NULL_HDBC) {
            SQLDisconnect(m_hDbc);
            SQLFreeHandle(SQL_HANDLE_DBC, m_hDbc);
        }
        if (m_hEnv != SQL_NULL_HENV) {
            SQLFreeHandle(SQL_HANDLE_ENV, m_hEnv);
        }
    }

    // ---------------------------------------------------------------
    // Register a user with their iris feature vector
    // eye: 0 = Left, 1 = Right
    // Returns the UserID
    // ---------------------------------------------------------------
    int registerUser(const std::wstring& username,
                     const std::wstring& fullName,
                     int eye,
                     const std::vector<float>& featureVector)
    {
        SQLHSTMT hStmt = SQL_NULL_HSTMT;
        checkRC(SQLAllocHandle(SQL_HANDLE_STMT, m_hDbc, &hStmt),
                m_hDbc, SQL_HANDLE_DBC, "Alloc STMT");

        const wchar_t* sql =
            L"EXEC sp_RegisterUser @Username=?, @FullName=?, @Eye=?, "
            L"@FeatureVector=?, @VectorSize=?, @NewUserID=? OUTPUT";

        checkRC(SQLPrepareW(hStmt, const_cast<SQLWCHAR*>(sql), SQL_NTS),
                hStmt, SQL_HANDLE_STMT, "Prepare sp_RegisterUser");

        // Bind @Username
        SQLLEN usernameLen = SQL_NTS;
        SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                         50, 0, const_cast<SQLWCHAR*>(username.c_str()), 0, &usernameLen);

        // Bind @FullName
        SQLLEN fullNameLen = SQL_NTS;
        SQLBindParameter(hStmt, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                         100, 0, const_cast<SQLWCHAR*>(fullName.c_str()), 0, &fullNameLen);

        // Bind @Eye
        SQLLEN eyeLen = 0;
        SQLSMALLINT eyeVal = static_cast<SQLSMALLINT>(eye);
        SQLBindParameter(hStmt, 3, SQL_PARAM_INPUT, SQL_C_SSHORT, SQL_TINYINT,
                         0, 0, &eyeVal, 0, &eyeLen);

        // Bind @FeatureVector (binary blob)
        SQLLEN vectorBytes = static_cast<SQLLEN>(featureVector.size() * sizeof(float));
        SQLBindParameter(hStmt, 4, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_VARBINARY,
                         vectorBytes, 0,
                         const_cast<float*>(featureVector.data()),
                         vectorBytes, &vectorBytes);

        // Bind @VectorSize
        SQLLEN vectorSizeLen = 0;
        SQLINTEGER vectorSizeVal = static_cast<SQLINTEGER>(featureVector.size());
        SQLBindParameter(hStmt, 5, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
                         0, 0, &vectorSizeVal, 0, &vectorSizeLen);

        // Bind @NewUserID OUTPUT
        SQLINTEGER newUserID = 0;
        SQLLEN newUserIDLen = sizeof(SQLINTEGER);
        SQLBindParameter(hStmt, 6, SQL_PARAM_OUTPUT, SQL_C_SLONG, SQL_INTEGER,
                         0, 0, &newUserID, sizeof(newUserID), &newUserIDLen);

        checkRC(SQLExecute(hStmt), hStmt, SQL_HANDLE_STMT, "Execute sp_RegisterUser");
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);

        return static_cast<int>(newUserID);
    }

    // ---------------------------------------------------------------
    // Load all registered iris feature vectors from the database
    // ---------------------------------------------------------------
    std::vector<IrisRecord> getAllIrisFeatures()
    {
        std::vector<IrisRecord> records;
        SQLHSTMT hStmt = SQL_NULL_HSTMT;
        checkRC(SQLAllocHandle(SQL_HANDLE_STMT, m_hDbc, &hStmt),
                m_hDbc, SQL_HANDLE_DBC, "Alloc STMT");

        checkRC(SQLExecDirectW(hStmt,
                               const_cast<SQLWCHAR*>(L"EXEC sp_GetAllIrisFeatures"),
                               SQL_NTS),
                hStmt, SQL_HANDLE_STMT, "Exec sp_GetAllIrisFeatures");

        while (SQLFetch(hStmt) == SQL_SUCCESS) {
            IrisRecord rec;
            SQLLEN ind = 0;

            // FeatureID
            SQLGetData(hStmt, 1, SQL_C_SLONG, &rec.featureID, sizeof(rec.featureID), &ind);
            // UserID
            SQLGetData(hStmt, 2, SQL_C_SLONG, &rec.userID, sizeof(rec.userID), &ind);
            // Username
            SQLWCHAR wbuf[256] = {};
            SQLGetData(hStmt, 3, SQL_C_WCHAR, wbuf, sizeof(wbuf), &ind);
            rec.username = wbuf;
            // FullName
            SQLWCHAR wbuf2[256] = {};
            SQLGetData(hStmt, 4, SQL_C_WCHAR, wbuf2, sizeof(wbuf2), &ind);
            rec.fullName = wbuf2;
            // Eye
            SQLSMALLINT eyeVal = 0;
            SQLGetData(hStmt, 5, SQL_C_SSHORT, &eyeVal, sizeof(eyeVal), &ind);
            rec.eye = eyeVal;
            // FeatureVector (binary)
            SQLLEN bytesAvailable = 0;
            SQLGetData(hStmt, 6, SQL_C_BINARY, nullptr, 0, &bytesAvailable);
            if (bytesAvailable > 0) {
                std::vector<BYTE> raw(bytesAvailable);
                SQLGetData(hStmt, 6, SQL_C_BINARY, raw.data(), bytesAvailable, &ind);
                size_t floatCount = bytesAvailable / sizeof(float);
                rec.featureVector.resize(floatCount);
                std::memcpy(rec.featureVector.data(), raw.data(), bytesAvailable);
            }
            // VectorSize (column 7) - already known from vector, skip
            records.push_back(std::move(rec));
        }

        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return records;
    }

    // ---------------------------------------------------------------
    // Log a recognition attempt
    // ---------------------------------------------------------------
    void logAttempt(int matchedUserID, int eye,
                    bool success, double confidenceScore,
                    const std::wstring& notes = L"")
    {
        SQLHSTMT hStmt = SQL_NULL_HSTMT;
        checkRC(SQLAllocHandle(SQL_HANDLE_STMT, m_hDbc, &hStmt),
                m_hDbc, SQL_HANDLE_DBC, "Alloc STMT");

        const wchar_t* sql =
            L"EXEC sp_LogRecognitionAttempt "
            L"@MatchedUserID=?, @Eye=?, @Success=?, @ConfidenceScore=?, @Notes=?";

        checkRC(SQLPrepareW(hStmt, const_cast<SQLWCHAR*>(sql), SQL_NTS),
                hStmt, SQL_HANDLE_STMT, "Prepare sp_LogRecognitionAttempt");

        SQLLEN userIDLen = 0;
        SQLINTEGER uid = matchedUserID;
        SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
                         0, 0, &uid, 0, &userIDLen);

        SQLLEN eyeLen = 0;
        SQLSMALLINT eyeVal = static_cast<SQLSMALLINT>(eye);
        SQLBindParameter(hStmt, 2, SQL_PARAM_INPUT, SQL_C_SSHORT, SQL_TINYINT,
                         0, 0, &eyeVal, 0, &eyeLen);

        SQLLEN successLen = 0;
        SQLSMALLINT successVal = success ? 1 : 0;
        SQLBindParameter(hStmt, 3, SQL_PARAM_INPUT, SQL_C_SSHORT, SQL_BIT,
                         0, 0, &successVal, 0, &successLen);

        SQLLEN scoreLen = 0;
        SQLDOUBLE score = confidenceScore;
        SQLBindParameter(hStmt, 4, SQL_PARAM_INPUT, SQL_C_DOUBLE, SQL_FLOAT,
                         0, 0, &score, 0, &scoreLen);

        SQLLEN notesLen = notes.empty() ? SQL_NULL_DATA : SQL_NTS;
        SQLBindParameter(hStmt, 5, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                         255, 0, const_cast<SQLWCHAR*>(notes.c_str()), 0, &notesLen);

        checkRC(SQLExecute(hStmt), hStmt, SQL_HANDLE_STMT, "Execute sp_LogRecognitionAttempt");
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    }

private:
    SQLHENV m_hEnv;
    SQLHDBC m_hDbc;

    // Throws std::runtime_error with the ODBC diagnostic message on failure
    static void checkRC(SQLRETURN rc, SQLHANDLE handle, SQLSMALLINT handleType,
                        const char* context)
    {
        if (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO)
            return;

        SQLWCHAR state[6]   = {};
        SQLWCHAR msg[1024]  = {};
        SQLINTEGER nativeErr = 0;
        SQLSMALLINT msgLen   = 0;

        SQLGetDiagRecW(handleType, handle, 1, state, &nativeErr, msg,
                       static_cast<SQLSMALLINT>(sizeof(msg) / sizeof(SQLWCHAR)), &msgLen);

        // Convert to narrow string for exception
        char narrow[1024] = {};
        WideCharToMultiByte(CP_UTF8, 0, msg, -1, narrow, sizeof(narrow), nullptr, nullptr);

        throw std::runtime_error(std::string(context) + ": " + narrow);
    }
};
