#pragma once
#include <string>
using namespace std;

//סטטוסים אפשריים של תוצאת אימות ביומטרי
enum class AuthStatus {
    MATCH,
    NO_MATCH,
    LOW_QUALITY,
    USER_NOT_FOUND,
    DB_ERROR
};

// תוצאת אימות ביומטרי — מחזירה סטטוס, מרחק המינג, מזהה המשתמש שהתאים (אם קיים), והודעת שגיאה או מידע נוסף.
struct AuthResult {
    AuthStatus status = AuthStatus::NO_MATCH;
    double hammingDist = 1.0;
    int matchedUserID = -1;
    string matchedName;
    string flightNumber;
    string seatNumber;
    string message;
    
    bool isMatch() const { return status == AuthStatus::MATCH; }// פונקציה עזר שמחזירה true אם התוצאה היא התאמה מוצלחת
};
