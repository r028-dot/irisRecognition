#include "DatabaseManager.h"
#include <stdexcept>
#include <cstring>
using namespace std;

//פונקציה שאחראית על יצירת חיבור למסד הנתונים באמצעות ODBC. 
DatabaseManager::DatabaseManager(const wstring& connStr)
{
    checkRC(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_hEnv),
            m_hEnv, SQL_HANDLE_ENV, "Alloc ENV");
    checkRC(SQLSetEnvAttr(m_hEnv, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0),
            m_hEnv, SQL_HANDLE_ENV, "Set ODBC version");
    checkRC(SQLAllocHandle(SQL_HANDLE_DBC, m_hEnv, &m_hDbc),
            m_hEnv, SQL_HANDLE_ENV, "Alloc DBC");
    SQLSetConnectAttr(m_hDbc, SQL_ATTR_LOGIN_TIMEOUT,reinterpret_cast<SQLPOINTER>(10), 0);
    SQLWCHAR outStr[1024] = {};
    SQLSMALLINT outLen = 0;
    //פונקציית החיבור לדאטאבייס באמצעות ODBC. היא מנסה להתחבר עם מחרוזת החיבור הנתונה ומחזירה קוד שגיאה אם החיבור נכשל. אם החיבור מצליח, היא מאחסנת את פרטי החיבור במשתנים המתאימים.
    SQLRETURN rc = SQLDriverConnectW(m_hDbc, nullptr,const_cast<SQLWCHAR*>(connStr.c_str()), SQL_NTS,
                                     outStr, 1024, &outLen, SQL_DRIVER_NOPROMPT);
    checkRC(rc, m_hDbc, SQL_HANDLE_DBC, "Connect to SQL Server");
}

//דסטרקטור של המחלקה DatabaseManager. הוא אחראי על שחרור המשאבים שהוקצו במהלך חיי האובייקט, כולל ניתוק החיבור למסד הנתונים ושחרור הסביבה של ODBC.
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

//פונקציה האחראית על רישום משתמש חדש עם תבניות ביומטריות של קוד האייריס. היא מקבלת את מזהה הנוסע, שם מלא, לאום, ומערכים של קודי אייריס עבור העין השמאלית והימנית.:
int DatabaseManager::enrollUser(const string& passengerID,
                                const string& fullName,
                                const string& nationality,
                                const vector<IrisCode>& irisLeft,
                                const vector<IrisCode>& irisRight)
{
    if (irisLeft.empty() || irisRight.empty())
        throw runtime_error("enrollUser: at least one template per eye is required");
    SQLHSTMT hStmt = allocStmt();
    const wchar_t* sql = L"{CALL sp_EnrollUser(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    checkRC(SQLPrepareW(hStmt, const_cast<SQLWCHAR*>(sql), SQL_NTS),
            hStmt, SQL_HANDLE_STMT, "Prepare sp_EnrollUser");
    wstring wPassport = strToWide(passengerID);
    SQLLEN passLen = SQL_NTS;
    //פונקציות החיבור של הטקסט למשבצות המתאימות בפרוצדורה
    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     20, 0, const_cast<SQLWCHAR*>(wPassport.c_str()), 0, &passLen);
    wstring wFullName = strToWide(fullName);
    SQLLEN nameLen = SQL_NTS;
    SQLBindParameter(hStmt, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     100, 0, const_cast<SQLWCHAR*>(wFullName.c_str()), 0, &nameLen);
    wstring wNationality = strToWide(nationality);
    SQLLEN natLen = SQL_NTS;
    SQLBindParameter(hStmt, 3, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     50, 0, const_cast<SQLWCHAR*>(wNationality.c_str()), 0, &natLen);
    vector<uint8_t> leftBuf[3], rightBuf[3];
    SQLLEN leftLen[3] = {}, rightLen[3] = {};//מקצה מערכים לגדלים של שלושת התמונות
    
    //פונקציית למבדה שמקשרת את הפרמטרים של הפרוצדורה עם התמונות הביומטריות של המשתמש. היא מקבלת את מספר הפרמטר, את מערך התמונות, את המערך שבו יאוחסנו הנתונים המוצפנים, את המערך שבו יאוחסנו הגדלים של התמונות, ואת האינדקס של התמונה הנוכחית.
    auto bindBinary = [&](SQLUSMALLINT paramIdx,
                          const vector<IrisCode>& codes,
                          vector<uint8_t>* bufs,
                          SQLLEN* lens,
                          int slot)
    {
        if (slot < static_cast<int>(codes.size())) 
        {
            vector<uint8_t> raw = codes[slot].toBytes();
            bufs[slot] = m_dbEncryptor.encrypt(raw);
            lens[slot] = static_cast<SQLLEN>(bufs[slot].size());
            //מכניס את האיריס קוד המוצפן למיקום הנכון בפרוצדורה
            SQLBindParameter(hStmt, paramIdx, SQL_PARAM_INPUT,
                             SQL_C_BINARY, SQL_VARBINARY,
                             560, 0, bufs[slot].data(), 560, &lens[slot]);
        } 
        else // אם אין תמונה ביומטרית במיקום הנוכחי
        {
            lens[slot] = SQL_NULL_DATA;//מגדיר את האורך כ-SQL_NULL_DATA כדי לציין שאין נתונים
            //מכניס את האיריס קוד כ-null לפרוצדורה
            SQLBindParameter(hStmt, paramIdx, SQL_PARAM_INPUT,
                             SQL_C_BINARY, SQL_VARBINARY,
                             560, 0, nullptr, 0, &lens[slot]);
        }
    };

    // שליחת הפרמטרים ללמבדה- פרמטרים 4-6: תבניות עין שמאל 1, 2, 3
    bindBinary(4, irisLeft,  leftBuf,  leftLen,  0);
    bindBinary(5, irisLeft,  leftBuf,  leftLen,  1);
    bindBinary(6, irisLeft,  leftBuf,  leftLen,  2);

    // שליחת הפרמטרים ללמבדה- פרמטרים 7-9: תבניות עין ימין 1, 2, 3
    bindBinary(7, irisRight, rightBuf, rightLen, 0);
    bindBinary(8, irisRight, rightBuf, rightLen, 1);
    bindBinary(9, irisRight, rightBuf, rightLen, 2);

    // פרמטר 10: מזהה המשתמש החדש (OUTPUT)
    SQLINTEGER newUserID = 0;
    SQLLEN newUserIDLen = sizeof(SQLINTEGER);
    SQLBindParameter(hStmt, 10, SQL_PARAM_OUTPUT, SQL_C_SLONG, SQL_INTEGER,
                     0, 0, &newUserID, sizeof(newUserID), &newUserIDLen);
    checkRC(SQLExecute(hStmt), hStmt, SQL_HANDLE_STMT, "Execute sp_EnrollUser");
    freeStmt(hStmt);
    return static_cast<int>(newUserID);//החזרת מזהה המשתמש החדש שהתקבל מהפרוצדורה
}


// מחזיר את פרטי המשתמש כולל תבניות האייריס שלו. מחזיר nullopt אם לא נמצא משתמש עם מזהה הנוסע הנתון.
optional<User> DatabaseManager::getUserByID(const string& passengerID)
{
    SQLHSTMT hStmt = allocStmt();

    const wchar_t* sql = L"EXEC sp_GetUserByID @PassengerID=?";
    checkRC(SQLPrepareW(hStmt, const_cast<SQLWCHAR*>(sql), SQL_NTS),
            hStmt, SQL_HANDLE_STMT, "Prepare sp_GetUserByID");
    wstring wPassport = strToWide(passengerID);
    SQLLEN passLen = SQL_NTS;

    //מחבר את מזהה הנוסע כפרמטר קלט לפרוצדורה
    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     20, 0, const_cast<SQLWCHAR*>(wPassport.c_str()), 0, &passLen);
    checkRC(SQLExecute(hStmt), hStmt, SQL_HANDLE_STMT, "Execute sp_GetUserByID");

    User user;
    bool found = false;
    //לכל שורה שהתקבלה מה-DB, מחלץ את הנתונים של המשתמש ואת קוד האייריס שלו. אם אין נתונים, מחזיר nullopt.
    while (SQLFetch(hStmt) == SQL_SUCCESS)
    {
        //משתנים שישמשו לאחסון הנתונים המתקבלים מה-DB עבור מזהה המשתמש, שם מלא ולאום המשתמש.
        SQLLEN ind = 0;
        SQLINTEGER uid = 0;
        SQLGetData(hStmt, 1, SQL_C_SLONG, &uid, sizeof(uid), &ind);
        SQLWCHAR skipBuf[32] = {};
        SQLGetData(hStmt, 2, SQL_C_WCHAR, skipBuf, sizeof(skipBuf), &ind);
        SQLWCHAR wName[200] = {};
        SQLGetData(hStmt, 3, SQL_C_WCHAR, wName, sizeof(wName), &ind);
        SQLWCHAR wNat[100] = {};
        SQLGetData(hStmt, 4, SQL_C_WCHAR, wNat, sizeof(wNat), &ind);

        // אם זו השורה הראשונה שנמצאה, מעדכן את פרטי המשתמש. אחרת, מוסיף את קוד האייריס לרשימה של הקודים הקיימים.
        if (!found)
        {
            user.userID = static_cast<int>(uid);
            user.passengerID = passengerID;
            user.fullName = wstrToUtf8(wName);
            user.nationality = wstrToUtf8(wNat);
            found = true;
        }

        SQLSMALLINT eyeVal = 0;
        SQLGetData(hStmt, 5, SQL_C_SSHORT, &eyeVal, sizeof(eyeVal), &ind);

        //לכל שלושת העיניים (3 עיניים), מחלץ את הנתונים של קוד האייריס מהעמודות 6-8 של השורה הנוכחית.
        for (int col = 6; col <= 8; ++col)
        {
            uint8_t encBuf[560] = {};
            SQLLEN irisLen = 0;
            SQLRETURN rc = SQLGetData(hStmt, static_cast<SQLUSMALLINT>(col),
                                     SQL_C_BINARY, encBuf, sizeof(encBuf), &irisLen);
            //אם התקבלה שורה תקינה ויש נתונים עבור קוד האייריס, מפענח את הנתונים ומוסיף אותם לרשימת הקודים של המשתמש בהתאם לעין (שמאל או ימין).
            if ((rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) && irisLen > 16) 
            {
                vector<uint8_t> enc(encBuf, encBuf + irisLen);
                vector<uint8_t> raw = m_dbEncryptor.decrypt(enc);
                IrisCode code = IrisCode::fromBytes(raw.data(), raw.size());
                if (eyeVal == 0) user.irisCodesLeft.push_back(code);
                else if (eyeVal == 1) user.irisCodesRight.push_back(code);
            }
        }
    }

    freeStmt(hStmt);
    if (!found) return std::nullopt;
    return user;
}

//פונקציה שבודקת אם המשתמש קיים ופעיל במערכת
bool DatabaseManager::userExists(const string& passengerID)
{
    SQLHSTMT hStmt = allocStmt();//מקצה סטייטמנט ODBC כדי לבצע את השאילתה מול ה-DB

    const wchar_t* sql = L"SELECT COUNT(1) FROM Users WHERE PassengerID=? AND IsActive=1"; 
    checkRC(SQLPrepareW(hStmt, const_cast<SQLWCHAR*>(sql), SQL_NTS),
            hStmt, SQL_HANDLE_STMT, "Prepare userExists");

    wstring wPassport = strToWide(passengerID);
    SQLLEN passLen = SQL_NTS;
    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     20, 0, const_cast<SQLWCHAR*>(wPassport.c_str()), 0, &passLen);

    checkRC(SQLExecute(hStmt), hStmt, SQL_HANDLE_STMT, "Execute userExists");

    SQLINTEGER count = 0;
    SQLLEN ind = 0;
    if (SQLFetch(hStmt) == SQL_SUCCESS)
        SQLGetData(hStmt, 1, SQL_C_SLONG, &count, sizeof(count), &ind);

    freeStmt(hStmt);
    return count > 0;
}

//בודק אם למשתמש עם מזהה הנוסע הנתון יש גישה לשער עם השם הנתון.
 // הפונקציה קוראת לפרוצדורה שמחזירה האם הגישה מאושרת, מספר הטיסה, מספר המושב וסיבת הדחייה (אם לא מאושר).
GateAccessResult DatabaseManager::checkGateAccess(int userID, const string& gateName)
{
    GateAccessResult result;
    SQLHSTMT hStmt = allocStmt();

    const wchar_t* sql =
        L"EXEC sp_CheckGateAccess "
        L"@UserID=?, @GateName=?, @AccessGranted=?, @FlightNumber=?, @SeatNumber=?, @Reason=?";
    checkRC(SQLPrepareW(hStmt, const_cast<SQLWCHAR*>(sql), SQL_NTS),
            hStmt, SQL_HANDLE_STMT, "Prepare sp_CheckGateAccess");

    //מכניס את הפרמטרים של המשתמש והשער לפרוצדורה. הפרמטר הראשון הוא מזהה המשתמש, השני הוא שם השער, והשלושה האחרים הם פרמטרים שמחזירים את התוצאה של הבדיקה (גישה מאושרת, מספר טיסה, מספר מושב וסיבת דחייה).
    SQLINTEGER uid = userID;
    SQLLEN uidLen = 0;
    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
                     0, 0, &uid, 0, &uidLen);

    wstring wGate = strToWide(gateName);
    SQLLEN gateLen = SQL_NTS;
    SQLBindParameter(hStmt, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     10, 0, const_cast<SQLWCHAR*>(wGate.c_str()), 0, &gateLen);

    SQLCHAR accessGranted = 0;
    SQLLEN accessLen = sizeof(accessGranted);
    SQLBindParameter(hStmt, 3, SQL_PARAM_OUTPUT, SQL_C_BIT, SQL_BIT,
                     0, 0, &accessGranted, sizeof(accessGranted), &accessLen);

    SQLWCHAR wFlight[16] = {};
    SQLLEN flightLen = sizeof(wFlight);
    SQLBindParameter(hStmt, 4, SQL_PARAM_OUTPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     10, 0, wFlight, sizeof(wFlight), &flightLen);

    SQLWCHAR wSeat[8] = {};
    SQLLEN seatLen = sizeof(wSeat);
    SQLBindParameter(hStmt, 5, SQL_PARAM_OUTPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     5, 0, wSeat, sizeof(wSeat), &seatLen);

    SQLWCHAR wReason[256] = {};
    SQLLEN reasonLen = sizeof(wReason);
    SQLBindParameter(hStmt, 6, SQL_PARAM_OUTPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     255, 0, wReason, sizeof(wReason), &reasonLen);

    checkRC(SQLExecute(hStmt), hStmt, SQL_HANDLE_STMT, "Execute sp_CheckGateAccess");

    result.accessGranted = (accessGranted != 0);
    if (flightLen != SQL_NULL_DATA) result.flightNumber = wstrToUtf8(wFlight);
    if (seatLen != SQL_NULL_DATA) result.seatNumber = wstrToUtf8(wSeat);
    if (reasonLen != SQL_NULL_DATA) result.reason = wstrToUtf8(wReason);

    freeStmt(hStmt);
    return result;
}

//מקצה משתנה שמייצג את הפקודה (statement) שתישלח למסד הנתונים. המשתנה הזה משמש לניהול הפרמטרים והביצוע של הפקודה.
SQLHSTMT DatabaseManager::allocStmt() const
{
    SQLHSTMT hStmt = SQL_NULL_HSTMT;
    checkRC(SQLAllocHandle(SQL_HANDLE_STMT, m_hDbc, &hStmt),
            m_hDbc, SQL_HANDLE_DBC, "Alloc STMT");
    return hStmt;
}

//פונקציה שאחראית לשחרור המשאב של הפקודה (statement) לאחר השימוש בו. היא מקבלת את משתנה ה-statement ומוודאת שהוא לא NULL לפני שהיא משחררת אותו באמצעות קריאה לפונקציה המתאימה של ODBC.
void DatabaseManager::freeStmt(SQLHSTMT hStmt)
{
    if (hStmt != SQL_NULL_HSTMT)
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
}

//פונקציה שאחראית לבדוק אם קוד שהוחזר מפונקציות ODBC מציין הצלחה או כישלון. אם יש שגיאה, היא אוספת את פרטי השגיאה (כולל קוד שגיאה והודעת שגיאה) ומעלה חריגה עם מידע זה.
void DatabaseManager::checkRC(SQLRETURN rc, SQLHANDLE handle,
                               SQLSMALLINT handleType, const char* context)
{
    if (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO)
        return;
    // אם קוד ההחזרה מציין שגיאה, הפונקציה אוספת את פרטי השגיאה מה-ODBC ומעלה חריגה עם מידע זה. היא משתמשת במשתנים מקומיים כדי לאחסן את פרטי השגיאה, כולל מצב השגיאה, הודעת השגיאה, קוד השגיאה המקומי ואורך ההודעה.
    SQLWCHAR state[6] = {};
    SQLWCHAR msg[1024] = {};
    SQLINTEGER nativeErr = 0;
    SQLSMALLINT msgLen = 0;
    // קריאה לפונקציה של ODBC שמחזירה את פרטי השגיאה עבור ההקשר הנתון (handle) ומאחסנת אותם במשתנים המקומיים שהוגדרו קודם.
    SQLGetDiagRecW(handleType, handle, 1, state, &nativeErr, msg, 1024, &msgLen);
    char narrow[1024] = {};
    WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<wchar_t*>(msg), -1,
                        narrow, sizeof(narrow), nullptr, nullptr);
    throw runtime_error(string(context) + ": " + narrow);
}

//פונקציה שממירה מחרוזת בפורמט רחב (WideChar) לפורמט UTF-8. 
string DatabaseManager::wstrToUtf8(const SQLWCHAR* wstr)
{
    if (!wstr || wstr[0] == 0) return {};
    const wchar_t* p = reinterpret_cast<const wchar_t*>(wstr);
    int len = WideCharToMultiByte(CP_UTF8, 0, p, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, p, -1, result.data(), len, nullptr, nullptr);
    return result;//החזרת המחרוזת המומרת ל-UTF-8
}

//פונקציה שממירה מחרוזת בפורמט UTF-8 לפורמט רחב (WideChar).
wstring DatabaseManager::strToWide(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring result(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, result.data(), len);
    return result;
}
