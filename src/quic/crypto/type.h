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

using EVPCIPHERCTXPtr = CSmartPtr<EVP_CIPHER_CTX, EVP_CIPHER_CTX_free>;
using EVPAEADCTXPtr = CSmartPtr<EVP_AEAD_CTX, EVP_AEAD_CTX_free>;
}

#endif