#include "DatabaseManager.h"
#include <stdexcept>
#include <cstring>
using namespace std;

//פונקציה שאחראית על יצירת חיבור למסד הנתונים באמצעות ODBC. היא מקבלת מחרוזת חיבור ומבצעת את השלבים הבאים:
DatabaseManager::DatabaseManager(const wstring& connStr)
{
    checkRC(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_hEnv),
            m_hEnv, SQL_HANDLE_ENV, "Alloc ENV");//פונקציה שאחראית לזרוק ולדווח על שגיאה במקרה ויש שיגאה בפונקציה הפנימית- הפונקציה הפנימית אחראית על הייצור של סביבת העבודה 
    checkRC(SQLSetEnvAttr(m_hEnv, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0),
            m_hEnv, SQL_HANDLE_ENV, "Set ODBC version");//הגדרת גרסת ODBC 3 לסביבה
    checkRC(SQLAllocHandle(SQL_HANDLE_DBC, m_hEnv, &m_hDbc),
            m_hEnv, SQL_HANDLE_ENV, "Alloc DBC");//יצירת החיבור הפיזי לדאטא בייס בתוך סביבת העבודה שיצרנו קודם
    // הגדרת זמן המתנה לחיבור למסד הנתונים (10 שניות) כדי למנוע חיבור ארוך מדי במקרה של בעיות רשת או מסד נתונים לא זמין.
    SQLSetConnectAttr(m_hDbc, SQL_ATTR_LOGIN_TIMEOUT,reinterpret_cast<SQLPOINTER>(10), 0);
    SQLWCHAR    outStr[1024] = {};//מערך שיכיל את מחרוזת החיבור בפורמט רחב (WideChar) לאחר ניסיון החיבור
    SQLSMALLINT outLen       = 0;//משתנה שיכיל את אורך מחרוזת החיבור בפורמט רחב (WideChar) לאחר ניסיון החיבור
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

int DatabaseManager::enrollUser(const string& passengerID,
                                const string& fullName,
                                const string& nationality,
                                const vector<IrisCode>& irisLeft,
                                const vector<IrisCode>& irisRight)
{
    // וידוא שהתקבל לפחות קוד ביומטרי אחד לכל עין
    if (irisLeft.empty() || irisRight.empty())
        throw runtime_error("enrollUser: at least one template per eye is required");
    SQLHSTMT hStmt = allocStmt();// הקצאת מזהה פעולה (Statement Handle) מול ה-DB
    // הגדרת תבנית הקריאה לפרוצדורה (10 פרמטרים)
    const wchar_t* sql = L"{CALL sp_EnrollUser(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    // הכנת השאילתה בשרת ה-SQL לצורך אופטימיזציה ואבטחה
    checkRC(SQLPrepareW(hStmt, const_cast<SQLWCHAR*>(sql), SQL_NTS),
            hStmt, SQL_HANDLE_STMT, "Prepare sp_EnrollUser");
    //מילוי הפרמטרים בפרוצדורה
    wstring wPassport = strToWide(passengerID);//ממיר את מזהה הנוסע לפורמט רחב (WideChar) כדי להתאים לסוג הנתונים שהפרוצדורה מצפה לו. הפרמטר הראשון בפרוצדורה הוא מזהה הנוסע (PassengerID).
    SQLLEN passLen = SQL_NTS;//אורך מחרוזת מזהה הנוסע בפורמט רחב (WideChar). SQL_NTS מציין שהמחרוזת מסתיימת בתו NULL.
    //פונקציית החיבור של הטקסט- מזהה נוסע, למשבצתת מספר 1
    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     20, 0, const_cast<SQLWCHAR*>(wPassport.c_str()), 0, &passLen);
    wstring wFullName = strToWide(fullName);//ממיר את השם לפורמט רחב
    SQLLEN nameLen = SQL_NTS;//אורך המחרוזת, מסמן שמסיים ב null
    //מחבר את הטקסט- השם, למשבצת מספר 2 בפרוצדורה
    SQLBindParameter(hStmt, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     100, 0, const_cast<SQLWCHAR*>(wFullName.c_str()), 0, &nameLen);
    wstring wNationality = strToWide(nationality);//ממיר את הלאום לפורמט רחב
    SQLLEN       natLen       = SQL_NTS;//אורך המחרוזת, מסמן שמסיים ב null
    //מחבר את הטקסט- הלאום, למשבצת מספר 3 בפרוצדורה
    SQLBindParameter(hStmt, 3, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     50, 0, const_cast<SQLWCHAR*>(wNationality.c_str()), 0, &natLen);

    vector<uint8_t> leftBuf[3], rightBuf[3];//מקצה מערכים מסוג וקטור לשלושת התמונות
    SQLLEN leftLen[3] = {}, rightLen[3] = {};//מקצה מערכים לגדלים של שלושת התמונות
    //פונקציית למבדה שמקשרת את הפרמטרים של הפרוצדורה עם התמונות הביומטריות של המשתמש. היא מקבלת את מספר הפרמטר, את מערך התמונות, את המערך שבו יאוחסנו הנתונים המוצפנים, את המערך שבו יאוחסנו הגדלים של התמונות, ואת האינדקס של התמונה הנוכחית.
    auto bindBinary = [&](SQLUSMALLINT paramIdx,
                          const vector<IrisCode>& codes,
                          vector<uint8_t>* bufs,
                          SQLLEN* lens,
                          int slot)
    {
        if (slot < static_cast<int>(codes.size())) //אם יש תמונה ביומטרית במיקום הנתון, הצפן אותה ושמור את הנתונים המוצפנים במערך המתאים. אחרת, הגדר את האורך כ-SQL_NULL_DATA כדי לציין שאין נתונים.
        {
            vector<uint8_t> raw = codes[slot].toBytes();//המרת קוד האייריס למערך בתים (512 בתים)
            bufs[slot] = m_dbEncryptor.encrypt(raw);//הצפנת הנתונים באמצעות Encryptor ושמירתם במערך המתאים
            lens[slot] = static_cast<SQLLEN>(bufs[slot].size());//שמירת האורך של הנתונים המוצפנים במערך המתאים
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
    //מחבר את הפרמטר של מזהה המשתמש החדש כפרמטר OUTPUT כדי לקבל את הערך מהפרוצדורה לאחר ביצועה
    SQLBindParameter(hStmt, 10, SQL_PARAM_OUTPUT, SQL_C_SLONG, SQL_INTEGER,
                     0, 0, &newUserID, sizeof(newUserID), &newUserIDLen);
    //לוקח את הסטיטמנט וושולח אותו ל-DB . אם יש שגיאה בביצוע, זורק חריגה עם ההקשר המתאים.
    checkRC(SQLExecute(hStmt), hStmt, SQL_HANDLE_STMT, "Execute sp_EnrollUser");
    freeStmt(hStmt);//שחרור המשאבים של הסטיטמנט לאחר סיום השימוש בו
    return static_cast<int>(newUserID);//החזרת מזהה המשתמש החדש שהתקבל מהפרוצדורה
}


// מחזיר את פרטי המשתמש כולל תבניות האייריס שלו. מחזיר nullopt אם לא נמצא משתמש עם מזהה הנוסע הנתון.
optional<User> DatabaseManager::getUserByID(const string& passengerID)
{
    SQLHSTMT hStmt = allocStmt();//מקצה סטייטמנט ODBC כדי לבצע את השאילתה מול ה-DB

    const wchar_t* sql = L"EXEC sp_GetUserByID @PassengerID=?";//הגדרת השאילתה לקריאה לפרוצדורה שמחזירה את פרטי המשתמש לפי מזהה הנוסע. הפרוצדורה מצפה לפרמטר אחד: מזהה הנוסע (PassengerID).
    checkRC(SQLPrepareW(hStmt, const_cast<SQLWCHAR*>(sql), SQL_NTS),
            hStmt, SQL_HANDLE_STMT, "Prepare sp_GetUserByID");//הכנת השאילתה בשרת ה-SQL לצורך אופטימיזציה ואבטחה

    wstring wPassport = strToWide(passengerID);//המרת מזהה הנוסע לפורמט רחב (WideChar) כדי להתאים לסוג הנתונים שהפרוצדורה מצפה לו
    SQLLEN passLen = SQL_NTS;//אורך מחרוזת מזהה הנוסע בפורמט רחב (WideChar). SQL_NTS מציין שהמחרוזת מסתיימת בתו NULL.
    //מחבר את מזהה הנוסע כפרמטר קלט לפרוצדורה
    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     20, 0, const_cast<SQLWCHAR*>(wPassport.c_str()), 0, &passLen);
    //ביצוע השאילתה מול ה-DB. אם יש שגיאה בביצוע, זורק חריגה עם ההקשר המתאים.
    checkRC(SQLExecute(hStmt), hStmt, SQL_HANDLE_STMT, "Execute sp_GetUserByID");

    User user;//אובייקט שיכיל את פרטי המשתמש שיתקבלו מה-DB.
    bool found = false;//דגל שיציין אם נמצא משתמש עם מזהה הנוסע הנתון. 
    while (SQLFetch(hStmt) == SQL_SUCCESS) //לולאה שממשיכה לקרוא שורות מהתוצאה של השאילתה כל עוד יש שורות זמינות. כל שורה מייצגת משתמש שיכול להתאים למזהה הנוסע הנתון (אם יש יותר מאחד, נלקח הראשון בלבד).
    {
        SQLLEN ind = 0;//משתנה שישמש לאחסון האורך של הנתונים המתקבלים מה-DB. 

        SQLINTEGER uid = 0;//משתנה שישמש לאחסון מזהה המשתמש (UserID) שמתקבל מה-DB.
        SQLGetData(hStmt, 1, SQL_C_SLONG, &uid, sizeof(uid), &ind);//קריאה מהעמודה הראשונה של השורה הנוכחית שמכילה את מזהה המשתמש (UserID) והעתקתו למשתנה uid.
        SQLWCHAR skipBuf[32] = {};//מערך ביניים שישמש לדילוג על עמודות שאינן נדרשות. כאן מדובר על העמודה השנייה (PassengerID) שמתקבלת מה-DB אך אינה נחוצה להמשך העיבוד.
        SQLGetData(hStmt, 2, SQL_C_WCHAR, skipBuf, sizeof(skipBuf), &ind);//קריאה מהעמודה השנייה של השורה הנוכחית שמכילה את מזהה הנוסע (PassengerID) והעתקתו למערך הביניים skipBuf. המידע הזה אינו נדרש להמשך העיבוד ולכן הוא נשמר במערך ביניים בלבד.
        SQLWCHAR wName[200] = {};//מערך שישמש לאחסון שם מלא של המשתמש שמתקבל מה-DB.
        SQLGetData(hStmt, 3, SQL_C_WCHAR, wName, sizeof(wName), &ind);//קריאה מהעמודה השלישית של השורה הנוכחית שמכילה את השם המלא של המשתמש והעתקתו למערך wName.
        SQLWCHAR wNat[100] = {};//מערך שישמש לאחסון אזרחות המשתמש שמתקבלת מה-DB.
        SQLGetData(hStmt, 4, SQL_C_WCHAR, wNat, sizeof(wNat), &ind);//קריאה מהעמודה הרביעית של השורה הנוכחית שמכילה את האזרחות של המשתמש והעתקתה למערך wNat.

        if (!found) //אם עדיין לא נמצא משתמש מתאים, מלא את פרטי המשתמש באובייקט user. אם כבר נמצא משתמש מתאים (found == true), דלג על מילוי הפרטים כדי לשמור את הפרטים של המשתמש הראשון שנמצא.
        {
            user.userID = static_cast<int>(uid);//המרת מזהה המשתמש (UserID) מסוג SQLINTEGER לסוג int ואחסונו בשדה userID של האובייקט user.
            user.passengerID = passengerID;//אחסון מזהה הנוסע בשדה passengerID של האובייקט user.
            user.fullName = wstrToUtf8(wName);//המרת השם המלא מפורמט רחב (WideChar) לפורמט UTF-8 ואחסונו בשדה fullName של האובייקט user.
            user.nationality = wstrToUtf8(wNat);//המרת האזרחות מפורמט רחב (WideChar) לפורמט UTF-8 ואחסונו בשדה nationality של האובייקט user.
            found = true;//עדכון הדגל שמציין שנמצא משתמש מתאים.
        }

        SQLSMALLINT eyeVal = 0;//משתנה שישמש לאחסון ערך העין (0 = שמאל, 1 = ימין) שמתקבל מה-DB.
        SQLGetData(hStmt, 5, SQL_C_SSHORT, &eyeVal, sizeof(eyeVal), &ind);//קריאה מהעמודה החמישית של השורה הנוכחית שמכילה את ערך העין והעתקתו למשתנה eyeVal.

        for (int col = 6; col <= 8; ++col) //לולאה שממשיכה לקרוא את שלושת העמודות הבאות של השורה הנוכחית שמכילות את תבניות האייריס (IrisCode1, IrisCode2, IrisCode3). כל עמודה מכילה נתונים מוצפנים שצריך לפענח כדי לקבל את קוד האייריס הגולמי.
        {
            uint8_t encBuf[560] = {};   // מאגר לנתונים מוצפנים (IV+ciphertext)
            SQLLEN irisLen = 0;//משתנה שישמש לאחסון אורך הנתונים המתקבלים מה-DB עבור קוד האייריס.
           
            //קריאה מהעמודה הנוכחית של השורה הנוכחית שמכילה את הנתונים המוצפנים של קוד האייריס והעתקתם למערך encBuf. אורך הנתונים המתקבלים מאוחסן במשתנה irisLen.
            SQLRETURN rc = SQLGetData(hStmt, static_cast<SQLUSMALLINT>(col),
                                     SQL_C_BINARY, encBuf, sizeof(encBuf), &irisLen);
            //בדיקה אם הקריאה הצליחה (SQL_SUCCESS או SQL_SUCCESS_WITH_INFO) ואם אורך הנתונים גדול מ-16 בתים (כדי לוודא שיש נתונים תקינים). אם כן, פענח את הנתונים המוצפנים כדי לקבל את קוד האייריס הגולמי.
            if ((rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) && irisLen > 16) 
            {
                vector<uint8_t> enc(encBuf, encBuf + irisLen);//יצירת וקטור שמכיל את הנתונים המוצפנים של קוד האייריס מתוך מערך encBuf לפי האורך irisLen.
                vector<uint8_t> raw = m_dbEncryptor.decrypt(enc);//פענוח הנתונים המוצפנים באמצעות ה-DBEncryptor כדי לקבל את קוד האייריס הגולמי.
                IrisCode code = IrisCode::fromBytes(raw.data(), raw.size());//יצירת אובייקט IrisCode מקוד האייריס הגולמי באמצעות הפונקציה fromBytes.
                if (eyeVal == 0) user.irisCodesLeft.push_back(code);//אם ערך העין הוא 0 (עין שמאל), הוסף את קוד האייריס למערך irisCodesLeft של האובייקט user.
                else
                if (eyeVal == 1) user.irisCodesRight.push_back(code);
            }
        }
    }

    freeStmt(hStmt);//משחרר את המשאבים של הסטייטמנט לאחר סיום השימוש בו
    if (!found) return std::nullopt;
    return user;//מחזיר את האוביקט יוזר
}

//פונקציה שבודקת אם המשתמש קיים ופעיל במערכת
bool DatabaseManager::userExists(const string& passengerID)
{
    SQLHSTMT hStmt = allocStmt();//מקצה סטייטמנט ODBC כדי לבצע את השאילתה מול ה-DB

    const wchar_t* sql =
        L"SELECT COUNT(1) FROM Users WHERE PassengerID=? AND IsActive=1"; //הגדרת השאילתה שבודקת האם המשתמש קיים ופעיל במערכת
    checkRC(SQLPrepareW(hStmt, const_cast<SQLWCHAR*>(sql), SQL_NTS),
            hStmt, SQL_HANDLE_STMT, "Prepare userExists");//הכנת השאילתה בשרת ה-SQL לצורך אופטימיזציה ואבטחה

    wstring wPassport = strToWide(passengerID);//ממיר לפורמט רחב
    SQLLEN passLen = SQL_NTS;
    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     20, 0, const_cast<SQLWCHAR*>(wPassport.c_str()), 0, &passLen);//מחבר את מזהה הנוסע כפרמטר קלט לשאילתה

    checkRC(SQLExecute(hStmt), hStmt, SQL_HANDLE_STMT, "Execute userExists");//ביצוע השאילתה מול ה-DB. אם יש שגיאה בביצוע, זורק חריגה עם ההקשר המתאים.

    SQLINTEGER count = 0;// משתנה שישמש לאחסון מספר המשתמשים שנמצאו במערכת עם מזהה הנוסע הנתון ופעילים. אם הערך גדול מ-0, המשתמש קיים ופעיל.
    SQLLEN ind = 0;//משתנה אחראי לשמירת האורך של נתונים
    if (SQLFetch(hStmt) == SQL_SUCCESS)
        SQLGetData(hStmt, 1, SQL_C_SLONG, &count, sizeof(count), &ind);

    freeStmt(hStmt);
    return count > 0;
}

// ---------------------------------------------------------------
// getAllIrisCodes  (reads IrisCode1/2/3 from single row)
// ---------------------------------------------------------------
std::vector<IrisCode> DatabaseManager::getAllIrisCodes(const std::string& passengerID,
                                                        int eye)
{
    std::vector<IrisCode> results;
    SQLHSTMT hStmt = allocStmt();

    const wchar_t* sql =
        L"SELECT f.IrisCode1, f.IrisCode2, f.IrisCode3 "
        L"FROM IrisFeatures f "
        L"JOIN Users u ON u.UserID = f.UserID "
        L"WHERE u.PassengerID = ? AND u.IsActive = 1 AND f.Eye = ?";
    checkRC(SQLPrepareW(hStmt, const_cast<SQLWCHAR*>(sql), SQL_NTS),
            hStmt, SQL_HANDLE_STMT, "Prepare getAllIrisCodes");

    std::wstring wPassport = strToWide(passengerID);
    SQLLEN       passLen   = SQL_NTS;
    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     20, 0, const_cast<SQLWCHAR*>(wPassport.c_str()), 0, &passLen);

    SQLLEN      eye_ind = 0;
    SQLSMALLINT eyeVal  = static_cast<SQLSMALLINT>(eye);
    SQLBindParameter(hStmt, 2, SQL_PARAM_INPUT, SQL_C_SSHORT, SQL_TINYINT,
                     0, 0, &eyeVal, 0, &eye_ind);

    checkRC(SQLExecute(hStmt), hStmt, SQL_HANDLE_STMT, "Execute getAllIrisCodes");

    if (SQLFetch(hStmt) == SQL_SUCCESS) {
        // Read up to 3 encrypted columns; decrypt each and add to results
        for (int col = 1; col <= 3; ++col) {
            uint8_t  encBuf[560] = {};   // מאגר לנתונים מוצפנים
            SQLLEN   irisLen     = 0;
            SQLRETURN rc = SQLGetData(hStmt, static_cast<SQLUSMALLINT>(col),
                                      SQL_C_BINARY, encBuf, sizeof(encBuf), &irisLen);
            if ((rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) && irisLen > 16) {
                // פענח לפני שימוש — IrisCode מוצפן תמיד בבסיס הנתונים
                std::vector<uint8_t> enc(encBuf, encBuf + irisLen);
                std::vector<uint8_t> raw = m_dbEncryptor.decrypt(enc);
                results.push_back(IrisCode::fromBytes(raw.data(), raw.size()));
            }
        }
    }
    freeStmt(hStmt);
    return results;
}

// ---------------------------------------------------------------
// checkGateAccess
// ---------------------------------------------------------------
GateAccessResult DatabaseManager::checkGateAccess(int userID, const std::string& gateName)
{
    GateAccessResult result;
    SQLHSTMT hStmt = allocStmt();

    const wchar_t* sql =
        L"EXEC sp_CheckGateAccess "
        L"@UserID=?, @GateName=?, @AccessGranted=?, @FlightNumber=?, @SeatNumber=?, @Reason=?";
    checkRC(SQLPrepareW(hStmt, const_cast<SQLWCHAR*>(sql), SQL_NTS),
            hStmt, SQL_HANDLE_STMT, "Prepare sp_CheckGateAccess");

    SQLINTEGER uid = userID;
    SQLLEN uidLen = 0;
    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
                     0, 0, &uid, 0, &uidLen);

    std::wstring wGate = strToWide(gateName);
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

//מקצה משתנה שמייצג את הפקודה (statement) שתישלח למסד הנתונים. המשתנה הזה משמש לניהול הפרמטרים והביצוע של הפקודה.
SQLHSTMT DatabaseManager::allocStmt() const
{
    SQLHSTMT hStmt = SQL_NULL_HSTMT;//הצהרה על משתנה שמייצג את הפקודה (statement) שתישלח למסד הנתונים, ומאתחל אותו לערך NULL
    checkRC(SQLAllocHandle(SQL_HANDLE_STMT, m_hDbc, &hStmt),
            m_hDbc, SQL_HANDLE_DBC, "Alloc STMT");//קריאה לפונקציה שמקצה משאב מסוג statement עבור החיבור למסד הנתונים. אם ההקצאה נכשלת, היא תזרוק חריגה עם פרטי השגיאה.
    return hStmt;//החזרת המשתנה שהוקצה, שמייצג את הפקודה (statement) המוכנה לשימוש לביצוע שאילתות או פקודות מול מסד הנתונים.
}

//פונקציה שאחראית לשחרור המשאב של הפקודה (statement) לאחר השימוש בו. היא מקבלת את משתנה ה-statement ומוודאת שהוא לא NULL לפני שהיא משחררת אותו באמצעות קריאה לפונקציה המתאימה של ODBC.
void DatabaseManager::freeStmt(SQLHSTMT hStmt)
{
    if (hStmt != SQL_NULL_HSTMT)//בדיקה אם משתנה ה-statement אינו NULL לפני ניסיון לשחרר אותו. זה מונע קריאה לפונקציה עם ערך לא חוקי שעלול לגרום לשגיאה.
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
}

//פונקציה שאחראית לבדוק אם קוד שהוחזר מפונקציות ODBC מציין הצלחה או כישלון. אם יש שגיאה, היא אוספת את פרטי השגיאה (כולל קוד שגיאה והודעת שגיאה) ומעלה חריגה עם מידע זה.
void DatabaseManager::checkRC(SQLRETURN rc, SQLHANDLE handle,
                               SQLSMALLINT handleType, const char* context)
{
    if (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO)// אם הפונקציה ODBC הצליחה, אין צורך להמשיך
        return;

    SQLWCHAR    state[6]    = {};//מערך שיכיל את קוד השגיאה
    SQLWCHAR    msg[1024]   = {};//מערך שיכיל את השגיאה שקרתה
    SQLINTEGER  nativeErr   = 0;// משתנה שיכיל את קוד השגיאה המקומי
    SQLSMALLINT msgLen      = 0;// משתנה שיכיל את אורך ההודעה
    SQLGetDiagRecW(handleType, handle, 1, state, &nativeErr, msg, 1024, &msgLen);// קריאה לפונקציה שמחזירה את פרטי השגיאה מה-ODBC
    char narrow[1024] = {};//מערך שיכיל את ההודעה המומרת ל-UTF-8
    WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<wchar_t*>(msg), -1,
                        narrow, sizeof(narrow), nullptr, nullptr);// המרת ההודעה מ-WideChar ל-UTF-8
    throw runtime_error(string(context) + ": " + narrow);// העלאת חריגה עם פרטי השגיאה
}

//פונקציה שממירה מחרוזת בפורמט רחב (WideChar) לפורמט UTF-8. 
string DatabaseManager::wstrToUtf8(const SQLWCHAR* wstr)
{
    if (!wstr || wstr[0] == 0) return {};//בדיקה אם המחרוזת היא NULL או ריקה, במקרה כזה מחזירה מחרוזת ריקה
    const wchar_t* p = reinterpret_cast<const wchar_t*>(wstr);//המרת המחרוזת מ-SQLWCHAR ל-wchar_t כדי להשתמש בפונקציות ההמרה של Windows
    int len = WideCharToMultiByte(CP_UTF8, 0, p, -1, nullptr, 0, nullptr, nullptr);//קריאה לפונקציה שמחשבת את האורך הנדרש למחרוזת UTF-8 שתתקבל מהמרת המחרוזת בפורמט רחב. היא מחזירה את מספר הבתים הדרושים, כולל תו הסיום NULL.
    if (len <= 1) return {};//אם האורך הנדרש הוא 1 או פחות (כלומר, המחרוזת בפורמט רחב הייתה ריקה או כללה רק תו NULL), מחזירה מחרוזת ריקה
    string result(static_cast<size_t>(len - 1), '\0');//יצירת מחרוזת ריקה באורך הנדרש פחות 1 (כדי לא לכלול את תו הסיום NULL) שתשמש לאחסון המחרוזת המומרת ל-UTF-8
    WideCharToMultiByte(CP_UTF8, 0, p, -1, result.data(), len, nullptr, nullptr);//קריאה לפונקציה שמבצעת את ההמרה בפועל ומאחסנת את התוצאה במחרוזת result
    return result;//החזרת המחרוזת המומרת ל-UTF-8
}

wstring DatabaseManager::strToWide(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring result(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, result.data(), len);
    return result;
}
