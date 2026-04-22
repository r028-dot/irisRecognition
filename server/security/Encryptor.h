#pragma once
#include <vector>
#include <cstdint>
#include <stdexcept>

// AES-256-CBC encrypt / decrypt using OpenSSL EVP.
// The 256-bit key is read once from the environment variable IRIS_AES_KEY
// (32 raw bytes encoded as 64 hex characters).
// A fresh random 16-byte IV is prepended to every ciphertext.
class Encryptor {
public:
    // Loads key from IRIS_AES_KEY env-var.  Throws if the variable is missing / invalid.
    Encryptor();

    // Returns: IV (16 bytes) || ciphertext
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext) const;

    // Expects: IV (16 bytes) || ciphertext  (as produced by encrypt)
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ivAndCipher) const;

private:
    uint8_t m_key[32] = {};

    static void parseHexKey(const char* hex, uint8_t* out32);
};
