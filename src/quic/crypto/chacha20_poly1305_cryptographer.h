#ifndef QUIC_CRYPTO_CHACHA20_POLY1305_CRYPTOGRAPHER
#define QUIC_CRYPTO_CHACHA20_POLY1305_CRYPTOGRAPHER

#include "quic/crypto/aead_base_cryptographer.h"

namespace quicx {
namespace quic {

class ChaCha20Poly1305Cryptographer: public AeadBaseCryptographer {
public:
    ChaCha20Poly1305Cryptographer();
    virtual ~ChaCha20Poly1305Cryptographer();

    const char* GetName() override;

    CryptographerId GetCipherId() override;

protected:
    // RFC 9001 §5.4.4: ChaCha20-Poly1305 derives the mask with the ChaCha20
    // block function directly (CRYPTO_chacha_20), not through an EVP_CIPHER,
    // so no cached HP context is required. Returning nullptr keeps the base
    // class from allocating one — and, more importantly, from ever guessing
    // AES-256-ECB because this suite's AEAD key is also 32 bytes.
    const EVP_CIPHER* GetHeaderProtectionCipher() const override;

    virtual bool MakeHeaderProtectMask(common::BufferSpan& sample, std::vector<uint8_t>& key, uint8_t* out_mask,
        size_t mask_cap, size_t& out_mask_length, EVP_CIPHER_CTX* cached_hp_ctx = nullptr) override;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CRYPTO_CHACHA20_POLY1305_CRYPTOGRAPHER