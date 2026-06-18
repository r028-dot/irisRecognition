#pragma once
#include <string>
using namespace std;
//תוצאות של ניסיון זיהוי ביומטרי, כולל סטטוס, מרחק המינג, מזהה משתמש תואם (אם קיים) והודעת שגיאה (אם רלוונטי)
enum class AuthStatus {
    MATCH,// התאמה מוצלחת
    NO_MATCH,// לא נמצאה התאמה
    LOW_QUALITY,// איכות נמוכה מדי לזיהוי
    USER_NOT_FOUND,// המשתמש לא נמצא
    DB_ERROR// שגיאת מסד נתונים
};

// מבנה שמכיל את תוצאות ניסיון הזיהוי, כולל סטטוס, מרחק המינג, מזהה משתמש תואם (אם קיים) והודעת שגיאה (אם רלוונטי)
struct AuthResult {
    AuthStatus status = AuthStatus::NO_MATCH;// סטטוס, ברירת מחדל הוא NO_MATCH
    double hammingDist   = 1.0;// מרחק המינג, ברירת מחדל הוא 1.0 (לא תואם)
    int matchedUserID = -1;// מזהה המשתמש שהתאים, -1 אם לא נמצא תואם
    string matchedName;// שם המשתמש שהתאים, ריק אם לא נמצא תואם
    string flightNumber;// מספר הטיסה שאושרה לשער זה, אם קיים
    string seatNumber;// מספר המושב של הנוסע בטיסה המאושרת, אם קיים
    string message;// הודעת שגיאה או מידע נוסף, ריקה אם אין שגיאה

    bool isMatch() const { return status == AuthStatus::MATCH; }// פונקציה עזר שמחזירה true אם התוצאה היא התאמה מוצלחת
};
