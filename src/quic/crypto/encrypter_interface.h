#ifndef QUIC_CRYPTO_DECRYPTER_INTERFACE
#define QUIC_CRYPTO_DECRYPTER_INTERFACE

#include <memory>

#include "quic/crypto/crypter_interface.h"

namespace quicx {

class IBufferReadOnly;
class EncrypterInterface:
    public CrypterIntreface {
public:
    EncrypterInterface() {}
    virtual ~EncrypterInterface() {}

    virtual bool Encrypt(std::shared_ptr<IBufferReadOnly> buffer) = 0;
};

}

#endif