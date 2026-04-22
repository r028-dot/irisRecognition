#include "Encryptor.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

// ── helpers ──────────────────────────────────────────────────────────────────
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
        out32[i] = static_cast<uint8_t>(
            (hexNibble(hex[2*i]) << 4) | hexNibble(hex[2*i+1]));
}

// ── Constructor ──────────────────────────────────────────────────────────────
Encryptor::Encryptor()
{
    const char* envKey = std::getenv("IRIS_AES_KEY");
    if (!envKey)
        throw std::runtime_error(
            "Encryptor: environment variable IRIS_AES_KEY is not set");
    parseHexKey(envKey, m_key);
}

// ── encrypt ──────────────────────────────────────────────────────────────────
std::vector<uint8_t> Encryptor::encrypt(const std::vector<uint8_t>& plaintext) const
{
    uint8_t iv[16];
    if (RAND_bytes(iv, sizeof(iv)) != 1)
        throw std::runtime_error("Encryptor: RAND_bytes failed");

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("Encryptor: EVP_CIPHER_CTX_new failed");

    std::vector<uint8_t> result(16);  // reserve space for IV
    std::memcpy(result.data(), iv, 16);

    // allocate space for ciphertext (plaintext + one extra AES block for padding)
    result.resize(16 + plaintext.size() + EVP_MAX_BLOCK_LENGTH);

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, m_key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Encryptor: EVP_EncryptInit_ex failed");
    }

    int outLen1 = 0;
    if (EVP_EncryptUpdate(ctx,
            result.data() + 16, &outLen1,
            plaintext.data(), static_cast<int>(plaintext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Encryptor: EVP_EncryptUpdate failed");
    }

    int outLen2 = 0;
    if (EVP_EncryptFinal_ex(ctx, result.data() + 16 + outLen1, &outLen2) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Encryptor: EVP_EncryptFinal_ex failed");
    }

    EVP_CIPHER_CTX_free(ctx);
    result.resize(16 + outLen1 + outLen2);
    return result;
}

// ── decrypt ──────────────────────────────────────────────────────────────────
std::vector<uint8_t> Encryptor::decrypt(const std::vector<uint8_t>& ivAndCipher) const
{
    if (ivAndCipher.size() < 17)  // IV(16) + at least 1 byte
        throw std::runtime_error("Encryptor: ciphertext too short");

    const uint8_t* iv         = ivAndCipher.data();
    const uint8_t* cipherData = ivAndCipher.data() + 16;
    int            cipherLen  = static_cast<int>(ivAndCipher.size() - 16);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("Encryptor: EVP_CIPHER_CTX_new failed");

    std::vector<uint8_t> plaintext(static_cast<size_t>(cipherLen + EVP_MAX_BLOCK_LENGTH));

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, m_key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Encryptor: EVP_DecryptInit_ex failed");
    }

    int outLen1 = 0;
    if (EVP_DecryptUpdate(ctx,
            plaintext.data(), &outLen1,
            cipherData, cipherLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Encryptor: EVP_DecryptUpdate failed");
    }

    int outLen2 = 0;
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + outLen1, &outLen2) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Encryptor: decryption failed (wrong key or corrupted data)");
    }

    EVP_CIPHER_CTX_free(ctx);
    plaintext.resize(static_cast<size_t>(outLen1 + outLen2));
    return plaintext;
}
