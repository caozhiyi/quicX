#ifndef QUIC_CRYPTO_AES_128_GCM_CRYPTOGRAPHER
#define QUIC_CRYPTO_AES_128_GCM_CRYPTOGRAPHER

#include "quic/crypto/aead_base_cryptographer.h"

namespace quicx {

class Aes128GcmCryptographer:
    public AeadBaseCryptographer {
public:
    Aes128GcmCryptographer();
    virtual ~Aes128GcmCryptographer();

    virtual const char* GetName();

    virtual uint32_t GetCipherId();
};

}

#endif