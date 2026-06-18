#pragma once
#include <string>
#include <vector>
#include "IrisCode.h"

// מייצג שורה מטבלת Users — משתמש
struct User {
    int         userID         = 0;
    std::string passengerID;
    std::string fullName;
    std::string nationality;
    bool        isActive       = true;// האם המשתמש פעיל (לא נמחק)

    // עד 3 תבניות רישום לכל עין (IrisCode1/2/3 מטבלת IrisFeatures)
    std::vector<IrisCode> irisCodesLeft;
    std::vector<IrisCode> irisCodesRight;

    bool hasLeft()  const { return !irisCodesLeft.empty();  }// האם יש לפחות תבנית אחת לעין שמאל
    bool hasRight() const { return !irisCodesRight.empty(); }// האם יש לפחות תבנית אחת לעין ימין
};
