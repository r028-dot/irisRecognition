#include "Encryptor.h"
#include <openssl/evp.h>
#include <openssl/err.h>
#include <stdexcept>
#include <string>
using namespace std;

namespace iris {

// ── עזר: זורק חריג עם הודעת שגיאה מ-OpenSSL ──────────────────────────────
static void throwOpenSSLError(const string& context)
{
    unsigned long err = ERR_get_error();
    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    throw std::runtime_error(context + ": " + buf);
}

// ── בנאי ──────────────────────────────────────────────────────────────────
Encryptor::Encryptor(const vector<uint8_t>& key,
                     const vector<uint8_t>& iv)
    : m_key(key), m_iv(iv)
{
    if (key.size() != 32)
        throw invalid_argument("AES-256 key must be 32 bytes");
    if (iv.size() != 16)
        throw invalid_argument("AES-CBC IV must be 16 bytes");
}

// ── encrypt ───────────────────────────────────────────────────────────────
// plaintext → ciphertext (AES-256-CBC, PKCS#7 padding)
vector<uint8_t> Encryptor::encrypt(const vector<uint8_t>& data) const
{
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        throwOpenSSLError("EVP_CIPHER_CTX_new");

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                           m_key.data(), m_iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throwOpenSSLError("EVP_EncryptInit_ex");
    }

    // גודל מקסימלי: plaintext + block שלם לpadding
    std::vector<uint8_t> ciphertext(data.size() + 16);
    int outLen1 = 0, outLen2 = 0;

    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &outLen1,
                          data.data(), static_cast<int>(data.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throwOpenSSLError("EVP_EncryptUpdate");
    }

    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + outLen1, &outLen2) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throwOpenSSLError("EVP_EncryptFinal_ex");
    }

    EVP_CIPHER_CTX_free(ctx);
    ciphertext.resize(static_cast<size_t>(outLen1 + outLen2));
    return ciphertext;
}

// ── decrypt ───────────────────────────────────────────────────────────────
// ciphertext → plaintext (AES-256-CBC, מסיר PKCS#7 padding)
vector<uint8_t> Encryptor::decrypt(const vector<uint8_t>& data) const
{
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        throwOpenSSLError("EVP_CIPHER_CTX_new");

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                           m_key.data(), m_iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throwOpenSSLError("EVP_DecryptInit_ex");
    }

    vector<uint8_t> plaintext(data.size());
    int outLen1 = 0, outLen2 = 0;

    if (EVP_DecryptUpdate(ctx, plaintext.data(), &outLen1,
                          data.data(), static_cast<int>(data.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throwOpenSSLError("EVP_DecryptUpdate");
    }

    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + outLen1, &outLen2) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throwOpenSSLError("EVP_DecryptFinal_ex (bad key/IV or corrupted data)");
    }

    EVP_CIPHER_CTX_free(ctx);
    plaintext.resize(static_cast<size_t>(outLen1 + outLen2));
    return plaintext;
}

} // namespace iris
