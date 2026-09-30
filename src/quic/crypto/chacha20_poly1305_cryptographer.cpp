#include <cstring>

#include <openssl/aead.h>
#include <openssl/chacha.h>
#include <openssl/evp.h>

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

ChaCha20Poly1305Cryptographer::~ChaCha20Poly1305Cryptographer() {}

const char* ChaCha20Poly1305Cryptographer::GetName() {
    return "chacha20_poly1305_cryptographer";
}

CryptographerId ChaCha20Poly1305Cryptographer::GetCipherId() {
    return kCipherIdChaCha20Poly1305Sha256;
}

const EVP_CIPHER* ChaCha20Poly1305Cryptographer::GetHeaderProtectionCipher() const {
    // The mask is produced by CRYPTO_chacha_20 below; no EVP context needed.
    return nullptr;
}

bool ChaCha20Poly1305Cryptographer::MakeHeaderProtectMask(common::BufferSpan& sample, std::vector<uint8_t>& key,
    uint8_t* out_mask, size_t mask_cap, size_t& out_mask_length, EVP_CIPHER_CTX* /*cached_hp_ctx*/) {
    out_mask_length = 0;

    // mask_cap is part of the base-class contract (the base implementation
    // refuses to write past it); honour it here too instead of ignoring it.
    if (out_mask == nullptr || mask_cap < kHeaderProtectMaskLength) {
        return false;
    }
    // RFC 9001 §5.4.4: the sample is 16 bytes — 4 bytes of counter followed by
    // 12 bytes of nonce. Every caller checks this before building the span (see
    // rtt_1_packet.cpp / init_packet.cpp), but the check is cheap and this
    // function is the last line of defence before reading raw memory.
    if (!sample.Valid() || sample.GetLength() < kHeaderProtectSampleLength) {
        return false;
    }

    const uint8_t* sample_pos = sample.GetStart();

    // memcpy, not a reinterpret_cast: the sample is an arbitrary offset into a
    // packet buffer, so it is not guaranteed to be 4-byte aligned, and reading
    // it through a uint32_t* would also violate strict aliasing (UB even where
    // it happens to work).
    uint32_t counter = 0;
    memcpy(&counter, sample_pos, sizeof(counter));
    sample_pos += sizeof(counter);

    CRYPTO_chacha_20(out_mask, kHeaderMask.data(), kHeaderMask.size(), key.data(), sample_pos, counter);

    out_mask_length = kHeaderMask.size();
    return true;
}

}  // namespace quic
}  // namespace quicx
