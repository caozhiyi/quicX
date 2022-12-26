#ifndef QUIC_CRYPTO_CHACHA20_POLY1305_CRYPTOGRAPHER
#define QUIC_CRYPTO_CHACHA20_POLY1305_CRYPTOGRAPHER

#include "quic/crypto/aead_base_cryptographer.h"

namespace quicx {

class ChaCha20Poly1305Cryptographer:
    public AeadBaseCryptographer {
public:
    ChaCha20Poly1305Cryptographer();
    virtual ~ChaCha20Poly1305Cryptographer();

    virtual const char* GetName();

    virtual uint32_t GetCipherId();
};

}

#endif