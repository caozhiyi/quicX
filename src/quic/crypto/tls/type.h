#ifndef QUIC_CRYPTO_TLS_TYPE
#define QUIC_CRYPTO_TLS_TYPE

#include <cstdint>
#include "openssl/ssl.h"
#include "common/util/c_smart_ptr.h"

namespace quicx {
namespace quic {

// Additional cipher suite IDs, not IANA-assigned.
#define TLS_AES_128_GCM_SHA256 0x1301
#define TLS_AES_256_GCM_SHA384 0x1302
#define TLS_CHACHA20_POLY1305_SHA256 0x1303

/* RFC 5116, 5.1 and RFC 8439, 2.3 for all supported ciphers */
#define QUIC_IV_LEN 12
/* RFC 9001, 5.4.1.  Header Protection Application: 5-byte mask */
#define QUIC_HP_LEN 5

// EncryptionLevel enumerates the stages of encryption that a QUIC connection
// progresses through. When retransmitting a packet, the encryption level needs
// to be specified so that it is retransmitted at a level which the peer can
// understand.
enum EncryptionLevel: int8_t {
    kInitial             = 0,
    kEarlyData           = 1,
    kHandshake           = 2,
    kApplication         = 3,
    kNumEncryptionLevels = 4,
};

using SSLCtxPtr = common::CSmartPtr<SSL_CTX, SSL_CTX_free>;
using SSLPtr = common::CSmartPtr<SSL, SSL_free>;

}
}

#endif