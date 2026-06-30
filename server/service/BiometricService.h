#pragma once
#include <memory>
#include <string>
#include <vector>
#include "../database/IUserRepository.h"
#include "../models/AuthResult.h"
#include "../iris/IrisProcessor.h"
using namespace std;

// שירות אימות ביומטריה - שכבת שירות בין API ל-DB
class BiometricService {
public:
    explicit BiometricService(shared_ptr<IUserRepository> db, 
                              int normWidth, int normHeight, 
                              double matchThreshold,
                              int minValidProbes = 2);

    // אימות 1:1 — multi-shot עם score-level fusion
    AuthResult verify(const string& passengerID,
                      const vector<vector<uint8_t>>& imageDataList,
                      int eye);

    // אימות לשער: בדיקה ביומטרית dual-eye (ממוצע HD שתי עיניים) + בדיקת הרשאות מעבר לשער
    AuthResult verifyForGate(const string& claimedPassengerID,
                             const string& gateName,
                             const vector<vector<uint8_t>>& leftImages,
                             const vector<vector<uint8_t>>& rightImages);

    // רישום נוסע חדש: חילוץ תבניות ביומטריות ושמירה ב-DB
    AuthResult enroll(const string& passengerID,
                      const string& fullName,
                      const string& nationality,
                      const vector<vector<uint8_t>>& imagesLeft,
                      const vector<vector<uint8_t>>& imagesRight);

private:
    shared_ptr<IUserRepository> m_db;
    IrisProcessor m_processor;  // אלגוריתם בלבד — ללא DB
    double m_matchThreshold;
    int m_minValidProbes;

    static constexpr int MIN_VALID_BITS  = 700;  // מינימום ביטים לא-מוסווים לקוד תקין
};
