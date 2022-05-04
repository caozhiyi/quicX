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

class IPacket;
class Protector {
public:
    Protector();
    virtual ~Protector();

    // make initial secret
    bool MakeInitSecret(char* sercet, uint16_t length);

    // make data transmission secret
    bool MakeEncryptionSecret(bool is_write, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len);
    
    // discard key
    void DiscardKey(ssl_encryption_level_t level);
    // is key available?
    bool IsAvailableKey(ssl_encryption_level_t level);

    bool Decrypt(std::shared_ptr<IPacket> packet, ssl_encryption_level_t level);

private:
    static std::string HkdfExpand(const EVP_MD* digest, const char* label, uint8_t label_len, const std::string* secret, uint8_t out_len);
    static bool GetHeaderProtectMask(const EVP_CIPHER *cipher, const Secret& secret, u_char *sample,  u_char *out_mask);
    static bool ParsePacketNumber(u_char*& pos, uint32_t length, u_char* mask, )
    struct Ciphers {
        const EVP_AEAD*   _content_protect_evp_cipher;
        const EVP_CIPHER* _header_protect_evp_cipher;
        const EVP_MD*     _evp_md;
    };
    static uint32_t GetCiphers(uint32_t id, enum ssl_encryption_level_t level, Ciphers& ciphers);

private:
    uint32_t _cipher;
    uint64_t _largest_packet_number;
    SecretPair _secrets[__secret_num];
};

}

#endif