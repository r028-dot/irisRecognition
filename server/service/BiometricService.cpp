#include "BiometricService.h"
#include "../database/IUserRepository.h"
#include "../utils/AccessLogger.h"
#include <bitset>
#include <string>
#include <algorithm>
using namespace std;

BiometricService::BiometricService(std::shared_ptr<IUserRepository> db,
                                    double matchThreshold)
    : m_db(std::move(db))
    , m_processor()
    , m_matchThreshold(matchThreshold)
{}


AuthResult BiometricService::verify(const string& passengerID,
                                     const vector<vector<uint8_t>>& imageDataList,
                                     int eye)
{
    AuthResult result;
    if (imageDataList.empty()) {
        result.status  = AuthStatus::LOW_QUALITY;
        result.message = "No probe images supplied";
        return result;
    }

    auto userOpt = m_db->getUserByID(passengerID);
    if (!userOpt.has_value()) {
        result.status  = AuthStatus::USER_NOT_FOUND;
        result.message = "ID number not registered";
        AccessLogger::instance().logAccess(passengerID, "", eye, 1.0, false, "User not found");
        return result;
    }
    const User& user = *userOpt;
    const vector<IrisCode>& storedCodes =
        (eye == 0) ? user.irisCodesLeft : user.irisCodesRight;
    if (storedCodes.empty()) {
        result.status  = AuthStatus::USER_NOT_FOUND;
        result.message = "No iris code registered for the requested eye";
        AccessLogger::instance().logAccess(passengerID, "", eye, 1.0, false, "Missing eye code in DB");
        return result;
    }

    vector<double> perProbeMinHD;
    perProbeMinHD.reserve(imageDataList.size());
    int rejectedProbes = 0;

    for (const auto& imageData : imageDataList) {
        IrisCode probe = m_processor.extractCode(imageData);

        int validBits = 0;
        for (int i = 0; i < 256; ++i)
            validBits += static_cast<int>(bitset<8>(probe.mask[i]).count());

        if (validBits < MIN_VALID_BITS) {
            ++rejectedProbes;
        }
        else
        {
        double bestForThisProbe = 1.0;
        for (const IrisCode& stored : storedCodes)
            bestForThisProbe = min(bestForThisProbe,
                                        m_processor.compare(probe, stored));

        perProbeMinHD.push_back(bestForThisProbe);
        }
    }

    //אם מספר התמונות התקינות קטן מהמינימום הנדרש, נרשום שגיאה ונחזיר.
    if (static_cast<int>(perProbeMinHD.size()) < MIN_VALID_PROBES) {
        result.status        = AuthStatus::LOW_QUALITY;
        result.matchedUserID = user.userID;
        result.matchedName   = user.fullName;
        result.message = "Only " + to_string(perProbeMinHD.size()) +
                               " of " + to_string(imageDataList.size()) +
                               " probe images passed quality gate (minimum " +
                               to_string(MIN_VALID_PROBES) + " required)";
        AccessLogger::instance().logAccess(passengerID, "", eye, 1.0, false,
                                           "Too few valid probes: " +
                                           to_string(perProbeMinHD.size()));
        return result;
    }

    double sum = 0.0;
    for (double d : perProbeMinHD) sum += d;
    const double fusedHD = sum / static_cast<double>(perProbeMinHD.size());

    result.hammingDist = fusedHD;
    result.matchedUserID = user.userID;
    result.matchedName = user.fullName;

    if (fusedHD <= m_matchThreshold) {
        result.status  = AuthStatus::MATCH;
        result.message = "Identity verified (fused over " +
                          to_string(perProbeMinHD.size()) + " probes)";
    } else {
        result.status  = AuthStatus::NO_MATCH;
        result.message = "Iris does not match";
    }
    AccessLogger::instance().logAccess(passengerID, "", eye,
                                       fusedHD, result.status == AuthStatus::MATCH);
    return result;
}


AuthResult BiometricService::verifyForGate(const string& passengerID,
                                            const string& gateName,
                                            const vector<vector<uint8_t>>& leftImages,
                                            const vector<vector<uint8_t>>& rightImages)
{
    AuthResult result;
    // אימות ביומטרי ללא בדיקת שער — קבלת HD לכל עין
    AuthResult lRes = verify(passengerID, leftImages, 0);
    AuthResult rRes = verify(passengerID, rightImages, 1);

    // בדיקת שגיאות (משתמש לא נמצא, איכות נמוכה)
    if (lRes.status == AuthStatus::USER_NOT_FOUND ||
        rRes.status == AuthStatus::USER_NOT_FOUND) {
        result = (lRes.status == AuthStatus::USER_NOT_FOUND) ? lRes : rRes;
        result.message = "Verification failed: entered ID was not found in the system";
        return result;
    }
    if (lRes.status == AuthStatus::LOW_QUALITY &&
        rRes.status == AuthStatus::LOW_QUALITY) {
        result.status  = AuthStatus::LOW_QUALITY;
        result.message = "Both eyes failed quality gate";
        return result;
    }

    // בחירת ה-HD המינימלי (best eye wins) — עין באיכות נמוכה לא נכנסת לחישוב
    double minHD = 1.0;
    if (lRes.status != AuthStatus::LOW_QUALITY) minHD = min(minHD, lRes.hammingDist);
    if (rRes.status != AuthStatus::LOW_QUALITY) minHD = min(minHD, rRes.hammingDist);

    // נתוני הנוסע מהתשובה הראשונה הזמינה
    result.hammingDist = minHD;
    result.matchedUserID = (lRes.matchedUserID > 0) ? lRes.matchedUserID : rRes.matchedUserID;
    result.matchedName   = lRes.matchedName.empty() ? rRes.matchedName : lRes.matchedName;

    // החלטה ביומטרית לפי מינימום HD 
    if (minHD > m_matchThreshold) {
        result.status  = AuthStatus::NO_MATCH;
        result.message = "Biometric verification failed: iris does not match the entered ID";
        AccessLogger::instance().logAccess(passengerID, gateName, 2, minHD, false, "Dual-eye NO_MATCH");
        return result;
    }

    // בדיקת הרשאת שער
    GateAccessResult access = m_db->checkGateAccess(result.matchedUserID, gateName);
    result.flightNumber = access.flightNumber;
    result.seatNumber   = access.seatNumber;

    if (!access.accessGranted) {
        result.status  = AuthStatus::NO_MATCH;
        result.message = access.reason.empty()
            ? "Gate access denied: passenger is not allowed at this gate"
            : "Gate access denied: " + access.reason;
        AccessLogger::instance().logAccess(passengerID, gateName, 2,
                                           minHD, false, result.message);
        return result;
    }

    result.status  = AuthStatus::MATCH;
    result.message = "Biometric verification passed. Access granted at gate " + gateName;
    if (!result.flightNumber.empty())
        result.message += " for flight " + result.flightNumber;
    AccessLogger::instance().logAccess(passengerID, gateName, 2,
                                       minHD, true, "Dual-eye gate approved");
    return result;
}

// רישום נוסע חדש: חילוץ תבניות ביומטריות ושמירה ב-DB
AuthResult BiometricService::enroll(const string& passengerID,
                                     const string& fullName,
                                     const string& nationality,
                                     const vector<vector<uint8_t>>& imagesLeft,
                                     const vector<vector<uint8_t>>& imagesRight)
{
    AuthResult result;
    if (m_db->userExists(passengerID)) {
        result.status  = AuthStatus::NO_MATCH;
        result.message = "ID number already enrolled";
        return result;
    }
    vector<IrisCode> leftCodes, rightCodes;
    for (const auto& img : imagesLeft)
        leftCodes.push_back(m_processor.extractCode(img));
    for (const auto& img : imagesRight)
        rightCodes.push_back(m_processor.extractCode(img));

    int newUserID = m_db->enrollUser(passengerID, fullName, nationality,
                                     leftCodes, rightCodes);
    result.status = AuthStatus::MATCH; 
    result.matchedUserID = newUserID;
    result.message = "User enrolled (ID=" + to_string(newUserID) + ")";
    // כתיבת לוג שינוי על רישום נוסע חדש
    AccessLogger::instance().logChange("ENROLL", passengerID,
                                       "UserID=" + to_string(newUserID) +
                                       " Name=" + fullName);
    return result;
}
