#pragma once
#include <vector>
#include <cstdint>
#include <stdexcept>
using namespace std;
// מחלקה שמבצעת הצפנה/פענוח AES-256-CBC עם IV אקראי. המפתח נלקח ממשתנה סביבה (או ניתן כפרמטר).
class Encryptor {
public:
    // יוצר Encryptor עם מפתח שנלקח מהמשתנה הסביבה הנתון (או "IRIS_AES_KEY" כברירת מחדל). המפתח חייב להיות מחרוזת hex של 64 תווים (32 bytes).
    explicit Encryptor(const char* envVarName = "IRIS_AES_KEY");
    //פונקציה שמצפינה את המסר
    vector<uint8_t> encrypt(const vector<uint8_t>& plaintext) const;
  //פונקציה שמפענחת את המסר
    vector<uint8_t> decrypt(const vector<uint8_t>& ivAndCipher) const;
private:
    uint8_t m_key[32] = {};// מפתח AES-256 (32 bytes). מאוחסן כמערך של בתים בינאריים.
    static void parseHexKey(const char* hex, uint8_t* out32);// ממיר מחרוזת hex של 64 תווים ל-32 בתים בינאריים. זורק חריגה אם המחרוזת לא תקינה.
};
