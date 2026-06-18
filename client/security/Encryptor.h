#pragma once
#include <vector>
#include <cstdint>

namespace iris {

// AES-256-CBC encryption with random per-message IV.
// The 256-bit key is read from environment variable IRIS_AES_KEY
// (32 raw bytes encoded as 64 hex characters) — same scheme as the server.
class Encryptor {
public:
    // Loads key from IRIS_AES_KEY env-var. Throws on missing/invalid key.
    Encryptor();

    // Encrypts plaintext with a freshly generated random IV.
    //  out_iv:  receives the 16-byte IV (caller keeps it for the wire).
    //  returns: ciphertext only (no IV prefix).
    std::vector<std::uint8_t> encrypt(const std::vector<std::uint8_t>& plaintext,
                                      std::uint8_t out_iv[16]) const;

    // Encrypts plaintext using an explicit caller-supplied IV.
    // Used by multi-shot verify, where all probes in one request share one IV.
    std::vector<std::uint8_t> encryptWithIV(const std::vector<std::uint8_t>& plaintext,
                                            const std::uint8_t iv[16]) const;

    // Decrypts ciphertext using an explicit IV.
    std::vector<std::uint8_t> decrypt(const std::vector<std::uint8_t>& ciphertext,
                                      const std::uint8_t iv[16]) const;

private:
    std::uint8_t m_key[32] = {};

    static void parseHexKey(const char* hex, std::uint8_t* out32);
};

} // namespace iris
