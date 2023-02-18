#ifndef QUIC_CRYPTO_TYPE
#define QUIC_CRYPTO_TYPE

#include <cstdint>
#include <openssl/evp.h>
#include "common/util/c_smart_ptr.h"

namespace quicx {

// hkdf expand label
static const uint8_t __tls_label_client[] = "tls13 client in";
static const uint8_t __tls_label_server[] = "tls13 server in";
static const uint8_t __tls_label_key[]    = "tls13 quic key";
static const uint8_t __tls_label_hp[]     = "tls13 quic hp";
static const uint8_t __tls_label_iv[]     = "tls13 quic iv";
static const uint8_t __header_mask[]      = "\x00\x00\x00\x00\x00";

static const uint16_t __max_init_secret_length       = 32;
static const uint16_t __header_protect_sample_length = 16;
static const uint16_t __header_protect_mask_length   = 5;
static const uint16_t __packet_nonce_length          = 16;
static const uint16_t __crypto_level_count           = 4;

static const uint8_t __initial_slat[] = "\x38\x76\x2c\xf7\xf5\x59\x34\xb3\x4d\x17\x9a\xe6\xa4\xc8\x0c\xad\xcc\xbb\x7f\x0a";

enum CryptographerType: uint16_t {
    CT_TLS1_CK_AES_128_GCM_SHA256,
    CT_TLS1_CK_AES_256_GCM_SHA384,
    CT_TLS1_CK_CHACHA20_POLY1305_SHA256,
};

using EVPCIPHERCTXPtr = CSmartPtr<EVP_CIPHER_CTX, EVP_CIPHER_CTX_free>;
using EVPAEADCTXPtr = CSmartPtr<EVP_AEAD_CTX, EVP_AEAD_CTX_free>;
}

#endif