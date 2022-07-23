#ifndef QUIC_CRYPTO_AEAD_BASE_DECRYPTER
#define QUIC_CRYPTO_AEAD_BASE_DECRYPTER

#include <memory>

#include "quic/crypto/decrypter_interface.h"

namespace quicx {

class IBufferReadOnly;
class AeadBaseDecrypter:
    public DecrypterIntreface {
public:
    AeadBaseDecrypter() {}
    ~AeadBaseDecrypter() {}

    void SetSecret(const std::string& serret);

    void SetIV(const std::string& iv);

    void SetHeaderSecret(const std::string& secret);

    bool Decrypt(std::shared_ptr<IBufferReadOnly> buffer);
};

}

#endif