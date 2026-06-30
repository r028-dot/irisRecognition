#pragma once
#include <string>
#include <optional>
#include <vector>
#include "../models/User.h"
#include "../models/AuthResult.h"
using namespace std;

// תוצאת בדיקת הרשאה לשער.
struct GateAccessResult {
    bool accessGranted = false;
    string flightNumber;
    string seatNumber;
    string reason;
};

//  IUserRepository.h  –  ממשק מופשט לשכבת הנתונים
//  מאפשר ל-BiometricService לעבוד עם כל מימוש של מאגר נתונים
//  בלי תלות קונקרטית ב-DatabaseManager.
class IUserRepository {
public:
    virtual ~IUserRepository() = default;
    //  שאילתות 
    virtual std::optional<User> getUserByID(const string& passengerID) = 0;
    virtual bool userExists(const string& passengerID) = 0;
    virtual GateAccessResult checkGateAccess(int userID, const string& gateName) = 0;
    virtual int enrollUser(const string& passengerID,
                           const string& fullName,
                           const string& nationality,
                           const vector<IrisCode>& irisLeft,
                           const vector<IrisCode>& irisRight) = 0;
};
