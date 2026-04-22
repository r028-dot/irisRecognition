#include "IrisProcessor.h"
#include <bitset>
#include <string>

IrisProcessor::IrisProcessor(std::shared_ptr<DatabaseManager> db)
    : m_db(std::move(db))
{}

// ---------------------------------------------------------------
// verify
// ---------------------------------------------------------------
AuthResult IrisProcessor::verify(const std::string& passportNumber,
                                  const std::vector<uint8_t>& imageData,
                                  int eye)
{
    AuthResult result;

    // 1. Load stored iris code from DB
    auto userOpt = m_db->getUserByPassport(passportNumber);
    if (!userOpt.has_value()) {
        result.status  = AuthStatus::USER_NOT_FOUND;
        result.message = "Passport number not registered";
        m_db->logAuthAttempt(-1, eye, false, 1.0, "User not found");
        return result;
    }

    const User& user   = *userOpt;
    const IrisCode& stored = (eye == 0) ? user.irisCodeLeft : user.irisCodeRight;
    bool hasCode           = (eye == 0) ? user.hasLeft      : user.hasRight;

    if (!hasCode) {
        result.status  = AuthStatus::USER_NOT_FOUND;
        result.message = "No iris code registered for the requested eye";
        m_db->logAuthAttempt(user.userID, eye, false, 1.0, "Missing eye code in DB");
        return result;
    }

    // 2. Extract IrisCode from the incoming image
    IrisCode probe = m_extractor.extract(imageData);

    // 3. Check image quality (count valid mask bits)
    int validBits = 0;
    for (int i = 0; i < 256; ++i)
        validBits += static_cast<int>(std::bitset<8>(probe.mask[i]).count());

    if (validBits < MIN_VALID_BITS) {
        result.status  = AuthStatus::LOW_QUALITY;
        result.message = "Image quality too low (" + std::to_string(validBits) + " valid bits)";
        m_db->logAuthAttempt(user.userID, eye, false, 1.0, "Low quality");
        return result;
    }

    // 4. Compare probe vs stored IrisCode
    double dist         = m_matcher.compare(probe, stored);
    result.hammingDist  = dist;
    result.matchedUserID = user.userID;
    result.matchedName   = user.fullName;

    if (dist <= MATCH_THRESHOLD) {
        result.status  = AuthStatus::MATCH;
        result.message = "Identity verified";
    } else {
        result.status  = AuthStatus::NO_MATCH;
        result.message = "Iris does not match";
    }

    // 5. Log the attempt to RecognitionLog
    m_db->logAuthAttempt(user.userID, eye,
                          result.status == AuthStatus::MATCH,
                          dist);
    return result;
}

// ---------------------------------------------------------------
// enroll
// ---------------------------------------------------------------
AuthResult IrisProcessor::enroll(const std::string& passportNumber,
                                  const std::string& fullName,
                                  const std::string& nationality,
                                  const std::vector<uint8_t>& imageDataLeft,
                                  const std::vector<uint8_t>& imageDataRight)
{
    AuthResult result;

    // 1. Reject duplicate enrollments
    if (m_db->userExists(passportNumber)) {
        result.status  = AuthStatus::NO_MATCH;
        result.message = "Passport number already enrolled";
        return result;
    }

    // 2. Extract iris codes from both eye images
    IrisCode leftCode  = m_extractor.extract(imageDataLeft);
    IrisCode rightCode = m_extractor.extract(imageDataRight);

    // 3. Save user + both iris codes to DB
    int newUserID = m_db->enrollUser(passportNumber, fullName, nationality,
                                      leftCode, rightCode);

    result.status        = AuthStatus::MATCH;   // reused as "success"
    result.matchedUserID = newUserID;
    result.message       = "User enrolled (ID=" + std::to_string(newUserID) + ")";
    return result;
}
