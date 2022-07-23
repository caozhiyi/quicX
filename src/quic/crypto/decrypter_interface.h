#ifndef QUIC_CRYPTO_DECRYPTER_INTERFACE
#define QUIC_CRYPTO_DECRYPTER_INTERFACE

#include <memory>

#include "quic/crypto/crypter_interface.h"

namespace quicx {

class IBufferReadOnly;
class DecrypterIntreface:
    public CrypterIntreface {
public:
    DecrypterIntreface() {}
    virtual ~DecrypterIntreface() {}

    virtual bool Decrypt(std::shared_ptr<IBufferReadOnly> buffer) = 0;
};

}

#endif