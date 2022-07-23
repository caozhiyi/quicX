#include <string>
#include <cstring>
#include "openssl/evp.h"
#include "openssl/sha.h"
#include "openssl/ssl.h"
#include "openssl/hkdf.h"
#include "openssl/digest.h"
#include "openssl/chacha.h"

#include "common/log/log.h"
#include "quic/crypto/type.h"
#include "quic/common/constants.h"
#include "quic/crypto/protector.h"
#include "quic/packet/header_flag.h"
#include "quic/packet/header_interface.h"
#include "quic/packet/packet_interface.h"

namespace quicx {

Protector::Protector():
    _cipher(0),
    _largest_packet_number(0) {

}

Protector::~Protector() {

}

bool Protector::MakeInitSecret(char* sercet, uint16_t length) {
    static const char* salt = "\x38\x76\x2c\xf7\xf5\x59\x34\xb3\x4d\x17\x9a\xe6\xa4\xc8\x0c\xad\xcc\xbb\x7f\x0a";
    SecretPair& secret_pair = _secrets[ssl_encryption_initial];

    /*
     * RFC 9001, section 5.  Packet Protection
     *
     * Initial packets use AEAD_AES_128_GCM.  The hash function
     * for HKDF when deriving initial secrets and keys is SHA-256.
     */
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
        uint32_t     target_secret_len;
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
    Secret& secret_info = is_write ? _secrets[level]._server_secret : _secrets[level]._client_secret;
    _cipher = SSL_CIPHER_get_protocol_id(cipher);

    Ciphers ciphers;
    uint32_t key_len = GetCiphers(_cipher, level, ciphers);
    if (key_len == 0) {
        LOG_ERROR("get ciphers failed.");
        return false;
    }

    secret_info._secret = std::string((char*)secret, secret_len);
    struct {
        std::string  label;
        std::string* source_secret; 
        uint32_t     target_secret_len;
        std::string* target_secret;

    } keys_list[] = {
        {"tls13 quic key", &secret_info._secret, key_len,     &secret_info._key},
        {"tls13 quic iv",  &secret_info._secret, QUIC_IV_LEN, &secret_info._iv},
        {"tls13 quic hp",  &secret_info._secret, key_len,     &secret_info._hp},
    };
    for (uint32_t i = 0; i < (sizeof(keys_list) / sizeof(keys_list[0])); i++) {
        *keys_list[i].target_secret = HkdfExpand(ciphers._evp_md, keys_list[i].label.c_str(), keys_list[i].label.length(), keys_list[i].source_secret, keys_list[i].target_secret_len);
        if (keys_list[i].target_secret->length() == 0) {
            LOG_ERROR("hkdf expand secret failed. index:%d", i);
            return false;
        }
    }
    
    return true;
}

void Protector::DiscardKey(ssl_encryption_level_t level) {
    _secrets[level]._client_secret._key.clear();
}

bool Protector::IsAvailableKey(ssl_encryption_level_t level) {
    return !_secrets[level]._client_secret._key.empty();
}

bool Protector::Decrypt(std::shared_ptr<IPacket> packet, ssl_encryption_level_t level) {
    BufferView payload = packet->GetPayload();
    if (payload.IsEmpty()) {
        LOG_ERROR("empty payload to decrypt");
        return false;
    }
    
    char* data = payload.GetData();
    uint32_t data_len = payload.GetLength();
    if (data_len < __initial_tls_tag_len + 4) {
        return false;
    }

    // get ciphers
    Ciphers ciphers;
    uint32_t key_len = 0;
    key_len = GetCiphers(_cipher, level, ciphers);
    if (key_len == 0) {
        LOG_ERROR("get cipher failed. level:%d, cipher id:%d", level, _cipher);
        return false;
    }

    // get secrets
    const Secret& secret = _secrets[level]._client_secret;

    // header protection
    u_char* sample_pos = (u_char*)data + 4;
    u_char mask[__header_protect_lenght] = {0};
    std::shared_ptr<IHeader> header = packet->GetHeader();
    HeaderFlag& header_flag = header->GetHeaderFlag();

    if (!GetHeaderProtectMask(ciphers._header_protect_evp_cipher, secret, sample_pos, mask)) {
        LOG_ERROR("get header protect mask failed.");
        return false;
    }

    // head label de confusion
    uint8_t flag = header_flag.GetFlagUint();
    flag ^= mask[0] & (header_flag.IsShortHeaderFlag() ? 0x1F : 0x0F);
    header_flag.SetFlagUint(flag);

    // get packet number length
    uint32_t packet_number_length = 0;
    if (header_flag.IsShortHeaderFlag()) {
        packet_number_length = header_flag.GetShortHeaderFlag()._packet_number_length;
    } else {
        packet_number_length = header_flag.GetLongHeaderFlag()._packet_number_length;
    }
    
    // packet number protection
    u_char* pos = (u_char*)data;
    uint64_t packet_number = ParsePacketNumber(pos, packet_number_length, &mask[1], _largest_packet_number);
    packet->SetPacketNumber(packet_number);

    // packet protection 
        

    return true;
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

bool Protector::GetHeaderProtectMask(const EVP_CIPHER *cipher, const Secret& secret, u_char *sample,  u_char *out_mask) {
    uint32_t cnt = 0;
    u_char zero[__header_protect_lenght] = {0};
    memcpy(&cnt, sample, sizeof(uint32_t));

    if (cipher == (const EVP_CIPHER *) EVP_aead_chacha20_poly1305()) {
        CRYPTO_chacha_20(out_mask, zero, __header_protect_lenght, (const uint8_t*)secret._hp.c_str(), &sample[4], cnt);
        return true;
    }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        return false;
    }

    if (EVP_EncryptInit_ex(ctx, cipher, NULL, (const u_char*)secret._hp.c_str(), sample) != 1) {
        LOG_ERROR("EVP_EncryptInit_ex() failed");
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    int32_t outlen = 0;
    if (!EVP_EncryptUpdate(ctx, out_mask, &outlen, zero, __header_protect_lenght)) {
        LOG_ERROR("EVP_EncryptUpdate() failed");
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    if (!EVP_EncryptFinal_ex(ctx, out_mask + __header_protect_lenght, &outlen)) {
        LOG_ERROR("EVP_EncryptFinal_Ex() failed");
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    EVP_CIPHER_CTX_free(ctx);
    return true;
}

uint64_t Protector::ParsePacketNumber(u_char*& data, uint32_t length, u_char* mask, uint64_t larget_packet_number) {
    uint64_t pn_nbits = std::min<uint64_t>(length * 8, 62);

    u_char* pos = data;
    uint64_t truncated_pn = *pos++ ^ *mask++;
    while (--length) {
        truncated_pn = (truncated_pn << 8) + (*pos++ ^ *mask++);
    }
    data = pos;

    uint64_t expected_pn = larget_packet_number + 1;
    uint64_t pn_win = 1ULL << pn_nbits;
    uint64_t pn_hwin = pn_win / 2;
    uint64_t pn_mask = pn_win - 1;

    uint64_t candidate_pn = (expected_pn & ~pn_mask) | truncated_pn;

    if ((int64_t) candidate_pn <= (int64_t) (expected_pn - pn_hwin)
        && candidate_pn < (1ULL << 62) - pn_win) {
        candidate_pn += pn_win;

    } else if (candidate_pn > expected_pn + pn_hwin
               && candidate_pn >= pn_win) {
        candidate_pn -= pn_win;
    }

    larget_packet_number = std::max<uint64_t>(larget_packet_number, candidate_pn);
    return candidate_pn;
}

uint32_t Protector::GetCiphers(uint32_t id, enum ssl_encryption_level_t level, Ciphers& ciphers) {
    uint32_t len = 0;
      
    if (level == ssl_encryption_initial) {
        id = TLS_AES_128_GCM_SHA256;
    }

    switch (id) {
    case TLS_AES_128_GCM_SHA256:
        ciphers._content_protect_evp_cipher = EVP_aead_aes_128_gcm();
        ciphers._header_protect_evp_cipher = EVP_aes_128_ctr();
        ciphers._evp_md = EVP_sha256();
        len = 16;
        break;

    case TLS_AES_256_GCM_SHA384:
        ciphers._content_protect_evp_cipher = EVP_aead_aes_256_gcm();
        ciphers._header_protect_evp_cipher = EVP_aes_256_ctr();
        ciphers._evp_md = EVP_sha384();
        len = 32;
        break;

    case TLS_CHACHA20_POLY1305_SHA256:
        ciphers._content_protect_evp_cipher = EVP_aead_chacha20_poly1305();
        ciphers._header_protect_evp_cipher = (const EVP_CIPHER *) EVP_aead_chacha20_poly1305();
        ciphers._evp_md = EVP_sha256();
        len = 32;
        break;

    default:
        LOG_ERROR("unknow ciphers id:%d", id);
        return 0;
    }
    return len;
}

}