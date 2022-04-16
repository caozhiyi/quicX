#include <string>
#include <cstring>
#include "openssl/evp.h"
#include "openssl/sha.h"
#include "openssl/ssl.h"
#include "openssl/hkdf.h"

#include "common/log/log.h"
#include "quic/crypto/protector.h"

namespace quicx {

Protector::Protector(): _cipher(0) {

}

Protector::~Protector() {

}

bool Protector::MakeInitSecret(char* sercet, uint16_t length) {
    static const char* salt = "\x38\x76\x2c\xf7\xf5\x59\x34\xb3\x4d\x17\x9a\xe6\xa4\xc8\x0c\xad\xcc\xbb\x7f\x0a";
    SecretPair& secret_pair = _secrets[ssl_encryption_initial];

    auto digest = EVP_sha256();
    size_t is_len = SHA256_DIGEST_LENGTH;
    uint8_t is[SHA256_DIGEST_LENGTH];
    if (HKDF_extract(is, &is_len, digest, (uint8_t*)sercet, length, (uint8_t*)salt, sizeof(salt)) == 0) {
        LOG_ERROR("hkdf extract failed.");
        return false;
    }

    std::string secret = std::string((char*)is, is_len);

    struct {
        std::string  label;
        std::string* source_secret; 
        uint8_t      target_secret_len;
        std::string* target_secret;

    } keys_list[] = {
        {"tls13 client in", &secret,                             SHA256_DIGEST_LENGTH, &secret_pair._client_secret._secret},
        {"tls13 quic key",  &secret_pair._client_secret._secret, aes_128_key_length,   &secret_pair._client_secret._key},
        {"tls13 quic iv",   &secret_pair._client_secret._secret, iv_length,            &secret_pair._client_secret._iv},
        {"tls13 quic hp",   &secret_pair._client_secret._secret, aes_128_key_length,   &secret_pair._client_secret._hp},
        {"tls13 server in", &secret,                             SHA256_DIGEST_LENGTH, &secret_pair._server_secret._secret},
        {"tls13 quic key",  &secret_pair._server_secret._secret, aes_128_key_length,   &secret_pair._server_secret._key},
        {"tls13 quic iv",   &secret_pair._server_secret._secret, iv_length,            &secret_pair._server_secret._iv},
        {"tls13 quic hp",   &secret_pair._server_secret._secret, aes_128_key_length,   &secret_pair._server_secret._hp},
    };

    for (uint32_t i = 0; i < (sizeof(keys_list) / sizeof(keys_list[0])); i++) {
        *keys_list[i].target_secret = HkdfExpand(digest, keys_list[i].label.c_str(), keys_list[i].label.length(), keys_list[i].source_secret, keys_list[i].target_secret_len);
        if (keys_list[i].target_secret->length() == 0) {
            LOG_ERROR("hkdf expand secret failed. index:%d", i);
            return false;
        }
    }
    
    return true;
}

bool Protector::MakeEncryptionSecret(bool is_write, ssl_encryption_level_t level, const SSL_CIPHER *cipher, const uint8_t *secret, size_t secret_len) {
    Secret& secret = is_write ? _secrets[level]._server_secret : _secrets[level]._client_secret;
    _cipher = SSL_CIPHER_get_protocol_id(cipher);

    // todo
}

std::string Protector::HkdfExpand(const EVP_MD* digest, const char* label, uint8_t label_len, const std::string* secret, uint8_t out_len) {
    uint8_t info[20] = {0};
    info[0] = 0;
    info[1] = out_len;
    info[2] = label_len;
    uint8_t* p = (uint8_t*)memcpy(&info[3], label, label_len);
    uint8_t info_len = 3 + label_len + 1;

    uint8_t ret[EVP_MAX_MD_SIZE] = {0};
    if (HKDF_expand(ret, out_len, digest, (const uint8_t*)secret->c_str(), secret->length(), info, info_len) == 0) {
        LOG_ERROR("hkdf expand failed.");
        return "";
    }

    return std::string((char*)ret, out_len);
}

}