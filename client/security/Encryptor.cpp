#include "Encryptor.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
using namespace std;

namespace iris {

//בדיקה אם התו הוא ספרה hex ומחזירה את הערך שלו (0-15). אם לא, זורקת חריגה.
static uint8_t hexNibble(char c)
{
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
    throw std::runtime_error("Encryptor: invalid hex character in IRIS_AES_KEY");
}

void Encryptor::parseHexKey(const char* hex, uint8_t* out32)
{
    if (std::strlen(hex) != 64)
        throw std::runtime_error(
            "Encryptor: IRIS_AES_KEY must be exactly 64 hex chars (32 bytes)");
    for (int i = 0; i < 32; ++i)
        out32[i] = static_cast<std::uint8_t>(
            (hexNibble(hex[2*i]) << 4) | hexNibble(hex[2*i + 1]));
}

static void throwOpenSSLError(const std::string& context)
{
    unsigned long err = ERR_get_error();
    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    throw std::runtime_error(context + ": " + buf);
}

//בנאי המחלקה Encryptor: קורא את מפתח ההצפנה AES-256 מהמשתנה הסביבתי IRIS_AES_KEY (hex string באורך 64 תווים) וממיר אותו למערך בתים.
Encryptor::Encryptor()
{
    const char* envKey = getenv("IRIS_AES_KEY");
    if (!envKey)
        throw runtime_error(
            "Encryptor: environment variable IRIS_AES_KEY is not set");
    parseHexKey(envKey, m_key);
}

//הצפנה של טקסט רגיל (plaintext) עם יצירת IV אקראי. מחזירה את הטקסט המוצפן (ciphertext) וממלאת את out_iv ב-IV שנוצר.
vector<uint8_t>
Encryptor::encrypt(const vector<uint8_t>& plaintext,
                   uint8_t out_iv[16]) const
{
    if (RAND_bytes(out_iv, 16) != 1)
        throwOpenSSLError("RAND_bytes");
    return encryptWithIV(plaintext, out_iv);
}

// הצפנה של טקסט רגיל (plaintext) עם IV שניתן כקלט. מחזירה את הטקסט המוצפן (ciphertext).
vector<uint8_t> Encryptor::encryptWithIV(const vector<uint8_t>& plaintext,
                         const uint8_t iv[16]) const
{
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throwOpenSSLError("EVP_CIPHER_CTX_new");

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, m_key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throwOpenSSLError("EVP_EncryptInit_ex");
    }

    std::vector<std::uint8_t> cipher(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
    int outLen1 = 0, outLen2 = 0;

    if (EVP_EncryptUpdate(ctx, cipher.data(), &outLen1,
                          plaintext.data(),
                          static_cast<int>(plaintext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throwOpenSSLError("EVP_EncryptUpdate");
    }
    if (EVP_EncryptFinal_ex(ctx, cipher.data() + outLen1, &outLen2) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throwOpenSSLError("EVP_EncryptFinal_ex");
    }

    EVP_CIPHER_CTX_free(ctx);
    cipher.resize(static_cast<size_t>(outLen1 + outLen2));
    return cipher;
}

// מפענח טקסט מוצפן (ciphertext) עם IV שניתן כקלט. מחזירה את הטקסט המפוענח (plaintext).
vector<uint8_t> Encryptor::decrypt(const vector<uint8_t>& cipher,
                   const uint8_t iv[16]) const
{
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throwOpenSSLError("EVP_CIPHER_CTX_new");

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, m_key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throwOpenSSLError("EVP_DecryptInit_ex");
    }

    vector<uint8_t> plain(cipher.size() + EVP_MAX_BLOCK_LENGTH);
    int outLen1 = 0, outLen2 = 0;

    if (EVP_DecryptUpdate(ctx, plain.data(), &outLen1,
                          cipher.data(),
                          static_cast<int>(cipher.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throwOpenSSLError("EVP_DecryptUpdate");
    }
    if (EVP_DecryptFinal_ex(ctx, plain.data() + outLen1, &outLen2) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throwOpenSSLError("EVP_DecryptFinal_ex (bad key/IV or corrupted data)");
    }

    EVP_CIPHER_CTX_free(ctx);
    plain.resize(static_cast<size_t>(outLen1 + outLen2));
    return plain;
}

} 
