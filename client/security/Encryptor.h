#pragma once
#include <vector>
#include <cstdint>
using namespace std;

namespace iris {

// AES-256-CBC encryption/decryption via OpenSSL
class Encryptor {
public:
    // key: 32 bytes (256 bit), iv: 16 bytes
    Encryptor(const vector<uint8_t>& key,
              const vector<uint8_t>& iv);

    // plaintext -> ciphertext
    vector<uint8_t> encrypt(const vector<uint8_t>& data) const;

    // ciphertext -> plaintext
    vector<uint8_t> decrypt(const vector<uint8_t>& data) const;

private:
    vector<uint8_t> m_key;
    vector<uint8_t> m_iv;
};

} // namespace iris
