#include "ReplayGuard.h"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cmath>
using namespace std;

// ממיר מערך 16 בתים (nonce) למחרוזת הקסדצימלית באורך 32 תווים
string ReplayGuard::nonceToHex(const uint8_t nonce[16])
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i)
        oss << std::setw(2) << static_cast<unsigned>(nonce[i]);
    return oss.str();
}

// מנקה את המפה מ-nonces שפג תוקפם
void ReplayGuard::cleanup_locked()
{
    auto now = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    for (auto it = seen.begin(); it != seen.end(); ) {
        if (it->second < now)
            it = seen.erase(it);
        else
            ++it;
    }
}

// בודק אם nonce לא נמצא כבר, ואם לא — מוסיף לרשימה
// מחזיר true אם הבקשה תקינה, false אם מדובר ב-repeat attack
bool ReplayGuard::checkAndRegister(const uint8_t nonce[16], uint64_t requestTimestamp)
{
    auto now = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    // בדיקת חלון הזמן — נדחה בקשה ישנה מ-30 שניות או עתידית מדי
    int64_t ts = static_cast<int64_t>(requestTimestamp);
    if (abs(now - ts) > MAX_CLOCK_SKEW_SECONDS)
        return false;
    const string key = nonceToHex(nonce);
    lock_guard<mutex> lock(mtx);
    cleanup_locked();
    if (seen.count(key))
        return false; 
        
    // הוסף nonce עם פקיעת תוקף כפולה מחלון הזמן
    seen[key] = now + MAX_CLOCK_SKEW_SECONDS * 2;
    return true;
}
