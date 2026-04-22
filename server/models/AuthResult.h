#pragma once
#include <string>

enum class AuthStatus {
    MATCH,
    NO_MATCH,
    LOW_QUALITY,
    USER_NOT_FOUND,
    DB_ERROR
};

struct AuthResult {
    AuthStatus  status        = AuthStatus::NO_MATCH;
    double      hammingDist   = 1.0;
    int         matchedUserID = -1;
    std::string matchedName;
    std::string message;

    bool isMatch() const { return status == AuthStatus::MATCH; }
};
