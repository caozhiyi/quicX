#ifndef QUIC_CRYPTO_TYPE
#define QUIC_CRYPTO_TYPE

#include <cstdint>

namespace quicx {

// hkdf expand label
static const uint8_t __tls_label_client[] = "tls13 client in";
static const uint8_t __tls_label_server[] = "tls13 server in";
static const uint8_t __tls_label_key[]    = "tls13 quic key";
static const uint8_t __tls_label_hp[]     = "tls13 quic hp";
static const uint8_t __tls_label_iv[]     = "tls13 quic iv";

static const uint16_t __max_init_secret_length = 32;

}

#endif