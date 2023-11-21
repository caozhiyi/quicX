#ifndef QUIC_CRYPTO_CHACHA20_POLY1305_CRYPTOGRAPHER
#define QUIC_CRYPTO_CHACHA20_POLY1305_CRYPTOGRAPHER

#include "quic/crypto/aead_base_cryptographer.h"

namespace quicx {
namespace quic {

class ChaCha20Poly1305Cryptographer:
    public AeadBaseCryptographer {
public:
    ChaCha20Poly1305Cryptographer();
    virtual ~ChaCha20Poly1305Cryptographer();

    virtual const char* GetName();

    virtual CryptographerId GetCipherId();

protected:
    virtual bool MakeHeaderProtectMask(common::BufferReadView sample, std::vector<uint8_t>& key,
                            uint8_t* out_mask, size_t mask_cap, size_t& out_mask_length);
};

}
}

#endif