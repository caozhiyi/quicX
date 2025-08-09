#include <openssl/aead.h>
#include <openssl/evp.h>
#include <openssl/chacha.h>
#include "common/buffer/if_buffer.h"
#include "common/buffer/buffer_read_view.h"
#include "quic/crypto/chacha20_poly1305_cryptographer.h"

namespace quicx {
namespace quic {

ChaCha20Poly1305Cryptographer::ChaCha20Poly1305Cryptographer() {
    aead_ = EVP_aead_chacha20_poly1305();
    digest_ = EVP_sha256();

    aead_key_length_ = EVP_AEAD_key_length(aead_);
    aead_iv_length_ = EVP_AEAD_nonce_length(aead_);
    aead_tag_length_ = EVP_AEAD_max_tag_len(aead_);

    cipher_key_length_ = 32; 
    cipher_iv_length_ = 16;
}

ChaCha20Poly1305Cryptographer::~ChaCha20Poly1305Cryptographer() {

}

const char* ChaCha20Poly1305Cryptographer::GetName() {
    return "chacha20_poly1305_cryptographer";
}

CryptographerId ChaCha20Poly1305Cryptographer::GetCipherId() {
    return kCipherIdChaCha20Poly1305Sha256;
}

bool ChaCha20Poly1305Cryptographer::MakeHeaderProtectMask(common::BufferSpan& sample, std::vector<uint8_t>& key,
    uint8_t* out_mask, size_t mask_cap, size_t& out_mask_length) {

    const uint8_t* sample_pos = sample.GetStart();
    uint32_t *counter = (uint32_t *)(sample_pos);
    sample_pos += sizeof(uint32_t);

    CRYPTO_chacha_20(out_mask, kHeaderMask.data(), kHeaderMask.size(), key.data(), sample_pos, *counter);

    out_mask_length = kHeaderMask.size();
    return true;
}

}
}
