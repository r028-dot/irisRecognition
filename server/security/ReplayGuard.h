#pragma once
#include <unordered_map>
#include <mutex>
#include <string>
#include <cstdint>
using namespace std;

// מחלקה שמונעת replay attacks על ידי בדיקה של nonce ייחודי לכל בקשה. אם nonce כבר נרשם, הבקשה נדחית. בנוסף, בודקת שה-timestamp של הבקשה לא ישן או עתידי מדי (חלון זמן מותר של 30 שניות).
struct ReplayGuard {
    // חלון זמן מותר בשניות — בקשה ישנה מ-30 שניות נדחית
    static constexpr int64_t MAX_CLOCK_SKEW_SECONDS = 30;
    mutex mtx;
    // מפה: ייצוג hex של nonce → זמן פקיעת תוקף (Unix epoch)
    std::unordered_map<string, int64_t> seen;

    // בודק שה-timestamp תקין וה-nonce לא נראה קודם.
    // מחזיר false אם הבקשה ישנה/עתידית מדי, או כבר נראתה (replay).
    bool checkAndRegister(const uint8_t nonce[16], uint64_t requestTimestamp);

private:
    void cleanup_locked();// מנקה את המפה מ-nonces שפג תוקפם. מניח שה-mtx נעול.
    static string nonceToHex(const uint8_t nonce[16]);// ממיר מערך 16 בתים (nonce) למחרוזת הקסדצימלית באורך 32 תווים
};
