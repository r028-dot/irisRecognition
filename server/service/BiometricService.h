#pragma once
#include <memory>
#include <string>
#include <vector>
#include "../database/IUserRepository.h"
#include "../models/AuthResult.h"
#include "../iris/IrisProcessor.h"
using namespace std;
// שכבת שירות: מתאמת בין אלגוריתם הקשתית (IrisProcessor) לבין מאגר הנתונים (IUserRepository).
// IrisProcessor עצמו עוסק רק בחילוץ קוד ביומטרי ובהשוואה;
// BiometricService תלויה אך ורק בממשק IUserRepository — לא במימוש הקונקרטי.
class BiometricService {
public:
    explicit BiometricService(shared_ptr<IUserRepository> db, double matchThreshold = 0.32);

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
    IrisProcessor                     m_processor;  // אלגוריתם בלבד — ללא DB
    double                            m_matchThreshold;

    static constexpr int MIN_VALID_BITS  = 700;  // מינימום ביטים לא-מוסווים לקוד תקין
    static constexpr int MIN_VALID_PROBES = 2;    // מינימום תמונות תקינות לביצוע fusion
};
