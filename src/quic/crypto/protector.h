#ifndef QUIC_CRYPTO_PROTECTOR
#define QUIC_CRYPTO_PROTECTOR

#include <string>
#include "openssl/ssl.h"

namespace quicx {

struct Secret {
    std::string _secret;
    std::string _key;
    std::string _iv;
    std::string _hp;
};

struct SecretPair {
    Secret _client_secret;
    Secret _server_secret;
};

const static uint16_t __secret_num = (ssl_encryption_application) + 1;

class Protector {
public:
    Protector();
    ~Protector();

    // make initial secret
    bool MakeInitSecret(char* sercet, uint16_t length);

    // make data transmission secret
    bool MakeEncryptionSecret(bool is_write, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len);

private:
    static std::string HkdfExpand(const EVP_MD* digest, const char* label, uint8_t label_len, const std::string* secret, uint8_t out_len);

private:
    uint32_t   _cipher;
    SecretPair _secrets[__secret_num];
};

}

#endif