#include <openssl/aead.h>
#include <openssl/evp.h>
#include <openssl/chacha.h>
#include "common/buffer/buffer_interface.h"
#include "common/buffer/buffer_read_view.h"
#include "quic/crypto/chacha20_poly1305_cryptographer.h"

namespace quicx {

ChaCha20Poly1305Cryptographer::ChaCha20Poly1305Cryptographer() {
    _aead = EVP_aead_chacha20_poly1305();
    _digest = EVP_sha256();

    _aead_key_length = EVP_AEAD_key_length(_aead);
    _aead_iv_length = EVP_AEAD_nonce_length(_aead);
    _aead_tag_length = EVP_AEAD_max_tag_len(_aead);

    _cipher_key_length = 32; 
    _cipher_iv_length = 16;
}

ChaCha20Poly1305Cryptographer::~ChaCha20Poly1305Cryptographer() {

}

const char* ChaCha20Poly1305Cryptographer::GetName() {
    return "chacha20_poly1305_cryptographer";
}

CryptographerId ChaCha20Poly1305Cryptographer::GetCipherId() {
    return CI_TLS1_CK_CHACHA20_POLY1305_SHA256;
}

bool ChaCha20Poly1305Cryptographer::MakeHeaderProtectMask(BufferReadView sample, std::vector<uint8_t>& key,
    uint8_t* out_mask, size_t mask_cap, size_t& out_mask_length) {

    const uint8_t* sample_pos = sample.GetData();
    uint32_t *counter = (uint32_t *)(sample_pos);
    sample_pos += sizeof(uint32_t);

    CRYPTO_chacha_20(out_mask, __header_mask, sizeof(__header_mask) - 1, key.data(), sample_pos, *counter);

    out_mask_length = sizeof(__header_mask);
    return true;
}

}
