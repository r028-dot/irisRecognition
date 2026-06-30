#pragma once
#include <vector>
#include <cstdint>
using namespace std;
namespace iris {
class Encryptor {
public:
    // בונה את האובייקט Encryptor עם מפתח AES-256 שניתן כקלט (hex string באורך 64 תווים)
    Encryptor();
    
    // מגדיר את מפתח ההצפנה (AES-256) מהקלט hex string באורך 64 תווים
   vector<uint8_t> encrypt(const vector<uint8_t>& plaintext,
                                      uint8_t out_iv[16]) const;
    // מצפין את הטקסט המוצפן עם IV שניתן כקלט (16 בתים)
    vector<uint8_t> encryptWithIV(const vector<uint8_t>& plaintext,
                                            const uint8_t iv[16]) const;

    // מפענח את הטקסט המוצפן עם IV שניתן כקלט (16 בתים)
    vector<uint8_t> decrypt(const vector<uint8_t>& ciphertext,
                                      const uint8_t iv[16]) const;

private:
    uint8_t m_key[32] = {};

    static void parseHexKey(const char* hex, uint8_t* out32);
};

} 
