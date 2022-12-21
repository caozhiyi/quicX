#ifndef QUIC_CRYPTO_CRYPTER_INTERFACE
#define QUIC_CRYPTO_CRYPTER_INTERFACE

#include <string>
#include <memory>

namespace quicx {

class IBufferReadOnly;
class CryptographerIntreface {
public:
    CryptographerIntreface() {}
    virtual ~CryptographerIntreface() {}

    virtual bool DecryptPacket(std::shared_ptr<IBufferReadOnly> ciphertext,
                             std::shared_ptr<IBufferReadOnly> out_plaintext) = 0;

    virtual bool EncryptPacket(std::shared_ptr<IBufferReadOnly> plaintext,
                             std::shared_ptr<IBufferReadOnly> out_ciphertext) = 0;
};

}

#endif