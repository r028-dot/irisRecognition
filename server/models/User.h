#pragma once
#include <string>
#include "IrisCode.h"

struct User {
    int         userID         = 0;
    std::string passportNumber;
    std::string fullName;
    std::string nationality;
    IrisCode    irisCodeLeft;
    IrisCode    irisCodeRight;
    bool        hasLeft        = false;
    bool        hasRight       = false;
};
