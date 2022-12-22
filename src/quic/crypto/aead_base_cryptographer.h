#ifndef QUIC_CRYPTO_AEAD_BASE_CRYPTOGRAPHER
#define QUIC_CRYPTO_AEAD_BASE_CRYPTOGRAPHER

#include "quic/crypto/cryptographer_interface.h"

namespace quicx {

class AeadBaseCryptographer:
    public CryptographerIntreface {
public:
    AeadBaseCryptographer();
    virtual ~AeadBaseCryptographer();

    virtual bool DecryptPacket(std::shared_ptr<IBufferReadOnly> ciphertext,
                             std::shared_ptr<IBufferReadOnly> out_plaintext);

    virtual bool EncryptPacket(std::shared_ptr<IBufferReadOnly> plaintext,
                             std::shared_ptr<IBufferReadOnly> out_ciphertext);
};

}

#endif