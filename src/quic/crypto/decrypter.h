#ifndef QUIC_CRYPTO_DECRYPTER
#define QUIC_CRYPTO_DECRYPTER

#include "quic/crypto/crypter_interface.h"

class Decrypter:
    public CrypterIntreface {
public:
    virtual ~Decrypter() {}
};

#endif