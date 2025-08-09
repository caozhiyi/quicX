#ifndef QUIC_CRYPTO_AES_128_GCM_CRYPTOGRAPHER
#define QUIC_CRYPTO_AES_128_GCM_CRYPTOGRAPHER

// Keep headers opaque; implementation-specific includes live in .cpp
#include "quic/crypto/aead_base_cryptographer.h"

namespace quicx {
namespace quic {

class Aes128GcmCryptographer:
    public AeadBaseCryptographer {
public:
    Aes128GcmCryptographer();
    virtual ~Aes128GcmCryptographer();

    virtual const char* GetName();

    virtual CryptographerId GetCipherId();
};

}
}

#endif