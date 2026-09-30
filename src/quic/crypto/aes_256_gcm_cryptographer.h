#ifndef QUIC_CRYPTO_AES_256_GCM_CRYPTOGRAPHER
#define QUIC_CRYPTO_AES_256_GCM_CRYPTOGRAPHER

#include "quic/crypto/aead_base_cryptographer.h"

namespace quicx {
namespace quic {

class Aes256GcmCryptographer: public AeadBaseCryptographer {
public:
    Aes256GcmCryptographer();
    virtual ~Aes256GcmCryptographer();

    virtual const char* GetName();

    virtual CryptographerId GetCipherId();

protected:
    // RFC 9001 §5.4.3: AES-256-GCM uses AES-256-ECB for header protection.
    const EVP_CIPHER* GetHeaderProtectionCipher() const override;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CRYPTO_AES_256_GCM_CRYPTOGRAPHER