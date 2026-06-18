#pragma once
#include <string>
#include <vector>
using namespace std;
//הגדרות תצורה לשרת — כל הפרמטרים הניתנים להתאמה נמצאים כאן, עם ערכי ברירת מחדל סבירים.
struct ServerConfig {
    int port        = 9000;//פורט TCP להאזנה
    int numWorkers  = 0; //מספר threads ב-ThreadPool (0 לגילוי אוטומטי לפי CPU)
  
    vector<string> allowedIPs;//רשימה של אי פי מותרות להתחבר  — להגבלת גישה לשרת אם רשימה ריק כל אי פי יכול להתחבר 

    double      hammingThreshold  = 0.32;//סף מקסימלי למרחק מינג אם נמוך מהסף מתקבל התאמה ביומטרית מוצלחת
    int         normalizedWidth   = 512;//רוחב תקני לתמונות קלט (לפני חילוץ הקוד) — כל תמונה תותאם לגודל זה כדי לייצב את הביצועים של אלגוריתם הקשתית
    int         normalizedHeight  = 64;//גובה תקני לתמונות קלט (לפני חילוץ הקוד) — כל תמונה תותאם לגודל זה כדי לייצב את הביצועים של אלגוריתם הקשתית

    //מחרוזת חיבור לדאטא בייס
    std::wstring dbConnectionString =
        L"DRIVER={ODBC Driver 17 for SQL Server};"
        L"SERVER=lpc:.\\SQLEXPRESS;"
        L"DATABASE=IrisRecognitionDB;"
        L"Trusted_Connection=yes;"
        L"MARS_Connection=Yes;"   // מאפשר מספר תוצאות פעילות במקביל על אותו חיבור
        L"Connection Timeout=5;";

    static ServerConfig loadFromFile(const string& jsonPath);
};
