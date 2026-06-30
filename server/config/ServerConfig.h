#pragma once
#include <string>
#include <vector>
using namespace std;
//הגדרות תצורה לשרת — כל הפרמטרים הניתנים להתאמה נמצאים כאן, עם ערכי ברירת מחדל סבירים.
struct ServerConfig {

    int port = 9000; 
    int numWorkers = 0; 
    vector<string> allowedIPs; 
    double hammingThreshold  = 0.32; 
    int normalizedWidth = 512; 
    int normalizedHeight = 64; 

    // פרמטרים תפעוליים
    int maxVerifyImages = 3;     // מספר מקסימלי של תמונות לאימות
    int maxEnrollImages = 3;     // מספר מקסימלי של תמונות לרישום
    int minValidProbes = 2;     // מינימום תמונות תקינות לביצוע fusion 

    //מחרוזת חיבור לדאטא בייס
    wstring dbConnectionString =
        L"DRIVER={ODBC Driver 17 for SQL Server};"
        L"SERVER=lpc:.\\SQLEXPRESS;"
        L"DATABASE=IrisRecognitionDB;"
        L"Trusted_Connection=yes;"
        L"MARS_Connection=Yes;"   
        L"Connection Timeout=5;";

    static ServerConfig loadFromFile(const string& jsonPath);
};
