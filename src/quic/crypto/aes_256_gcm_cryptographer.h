#ifndef QUIC_CRYPTO_AES_256_GCM_CRYPTOGRAPHER
#define QUIC_CRYPTO_AES_256_GCM_CRYPTOGRAPHER

#include "quic/crypto/aead_base_cryptographer.h"

namespace quicx {

class Aes256GcmCryptographer:
    public AeadBaseCryptographer {
public:
    Aes256GcmCryptographer();
    virtual ~Aes256GcmCryptographer();

    virtual const char* GetName();

    virtual CryptographerId GetCipherId();
};

}

#endif