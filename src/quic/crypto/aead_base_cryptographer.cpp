#include <cstring>
#ifdef _WIN32
// Windows headers must be included in the correct order
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#endif
#include <openssl/aead.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include "common/log/log.h"
#include "quic/crypto/type.h"
#include "quic/crypto/hkdf.h"
#include "quic/crypto/aead_base_cryptographer.h"

namespace quicx {
namespace quic {

AeadBaseCryptographer::AeadBaseCryptographer():
    digest_(nullptr),
    aead_(nullptr),
    cipher_(nullptr) {

}

AeadBaseCryptographer::~AeadBaseCryptographer() {
    CleanSecret(read_secret_);
    CleanSecret(write_secret_);
}

ICryptographer::Result AeadBaseCryptographer::InstallSecret(const uint8_t* secret, size_t secret_len, bool is_write) {
    Secret& dest_secret = is_write ? write_secret_ : read_secret_;
    
    // make packet protect key
    dest_secret.key_.resize(aead_key_length_);
    size_t len = 0;
    if (!Hkdf::HkdfExpand(dest_secret.key_.data(), aead_key_length_,  secret, secret_len, kTlsLabelKey.data(), kTlsLabelKey.size(), digest_)) {
        CleanSecret(dest_secret);
        return Result::kDeriveFailed;
    }

    // make packet protect iv
    dest_secret.iv_.resize(aead_iv_length_);
    if (!Hkdf::HkdfExpand(dest_secret.iv_.data(), aead_iv_length_,  secret, secret_len, kTlsLabelIv.data(), kTlsLabelIv.size(), digest_)) {
        CleanSecret(dest_secret);
        return Result::kDeriveFailed;
    }

    // make header protext key
    dest_secret.hp_.resize(cipher_key_length_);
    if (!Hkdf::HkdfExpand(dest_secret.hp_.data(), cipher_key_length_,  secret, secret_len, kTlsLabelHp.data(), kTlsLabelHp.size(), digest_)) {
        CleanSecret(dest_secret);
        return Result::kDeriveFailed;
    }
    // Initialize or refresh cached contexts
    if (is_write) {
        write_aead_ctx_.reset(EVP_AEAD_CTX_new(aead_, dest_secret.key_.data(), dest_secret.key_.size(), aead_tag_length_));
        hp_write_ctx_.reset(EVP_CIPHER_CTX_new());
        if (hp_write_ctx_.get()) {
            EVP_EncryptInit_ex(hp_write_ctx_.get(), cipher_, NULL, dest_secret.hp_.data(), kHeaderMask.data());
        }
    } else {
        read_aead_ctx_.reset(EVP_AEAD_CTX_new(aead_, dest_secret.key_.data(), dest_secret.key_.size(), aead_tag_length_));
        hp_read_ctx_.reset(EVP_CIPHER_CTX_new());
        if (hp_read_ctx_.get()) {
            EVP_EncryptInit_ex(hp_read_ctx_.get(), cipher_, NULL, dest_secret.hp_.data(), kHeaderMask.data());
        }
    }
    return Result::kOk;
}

ICryptographer::Result AeadBaseCryptographer::KeyUpdate(const uint8_t* new_base_secret, size_t secret_len, bool update_write) {
    // RFC 9001 §6: next_secret = HKDF-Expand-Label(current_secret, "tls13 quic ku", "", hash_len)
    // Here we allow caller to pass a base secret; if null, derive from current read/write secret.
    const Secret& current = update_write ? write_secret_ : read_secret_;
    std::vector<uint8_t> base;
    if (new_base_secret && secret_len > 0) {
        base.assign(new_base_secret, new_base_secret + secret_len);
    } else {
        // Use current key as input secret for ku step (same hash as digest_)
        base = current.key_;
    }
    size_t hash_len = EVP_MD_size(digest_);
    std::vector<uint8_t> next_secret(hash_len);
    if (!Hkdf::HkdfExpand(next_secret.data(), next_secret.size(), base.data(), base.size(),
                          kTlsLabelKu.data(), kTlsLabelKu.size(), digest_)) {
        return Result::kDeriveFailed;
    }
    // Reinstall secrets using next_secret
    auto r = InstallSecret(next_secret.data(), next_secret.size(), update_write);
    if (r == Result::kOk) key_updated_flag_ = true;
    return r;
}

ICryptographer::Result AeadBaseCryptographer::InstallInitSecret(const uint8_t* secret, size_t secret_len, const uint8_t *salt, size_t saltlen, bool is_server) {
    const EVP_MD *digest = EVP_sha256();

    // make init secret
    uint8_t init_secret[kMaxInitSecretLength] = {0};
    if (!Hkdf::HkdfExtract(init_secret, kMaxInitSecretLength, secret, secret_len, salt, saltlen, digest)) {
        return Result::kDeriveFailed;
    }

    // This code is described in RFC 9001, Section 5.2, which details the process of deriving initial secrets
    // using HKDF (HMAC-based Extract-and-Expand Key Derivation Function). The initial secrets are derived
    // from the initial secret, which is itself derived from the client's and server's connection IDs and
    // a fixed salt. The derived secrets are used to encrypt and decrypt packets during the initial phase
    // of a QUIC connection.
    const uint8_t* read_label = kTlsLabelClient.data();
    size_t read_label_len = kTlsLabelClient.size();
    const uint8_t* write_label = kTlsLabelServer.data();
    size_t write_label_len = kTlsLabelServer.size();

    if (!is_server) std::swap(read_label, write_label);
    if (!is_server) std::swap(read_label_len, write_label_len);

    uint8_t init_read_secret[kMaxInitSecretLength] = {0};
    if (!Hkdf::HkdfExpand(init_read_secret, kMaxInitSecretLength,  init_secret,
        kMaxInitSecretLength, read_label, read_label_len, digest)) {
        return Result::kDeriveFailed;
    }

    uint8_t init_write_secret[kMaxInitSecretLength] = {0};
    if (!Hkdf::HkdfExpand(init_write_secret, kMaxInitSecretLength,  init_secret,
        kMaxInitSecretLength, write_label, write_label_len, digest)) {
        return Result::kDeriveFailed;
    }

    if (InstallSecret(init_read_secret, kMaxInitSecretLength, false) != Result::kOk) {
        return Result::kDeriveFailed;
    }

    if (InstallSecret(init_write_secret, kMaxInitSecretLength, true) != Result::kOk) {
        return Result::kDeriveFailed;
    }

    return Result::kOk;
}

ICryptographer::Result AeadBaseCryptographer::DecryptPacket(uint64_t pkt_number, common::BufferSpan& associated_data, common::BufferSpan& ciphertext,
    std::shared_ptr<common::IBuffer> out_plaintext) {
    if (read_secret_.key_.empty() || read_secret_.iv_.empty()) {
        common::LOG_ERROR("decrypt packet but not install secret");
        return Result::kNotInitialized;
    }

    // get nonce
    uint8_t nonce[kPacketNonceLength] = {0};
    MakePacketNonce(nonce, read_secret_.iv_, pkt_number);

    // encrypt
    EVP_AEAD_CTX* raw = read_aead_ctx_.get();
    if (!raw) {
        read_aead_ctx_.reset(EVP_AEAD_CTX_new(aead_, read_secret_.key_.data(), read_secret_.key_.size(), aead_tag_length_));
        raw = read_aead_ctx_.get();
    }
    if (!raw) { 
        common::LOG_ERROR("EVP_AEAD_CTX_new failed");
        return Result::kInternalError;
    }

    size_t out_length = 0;
    auto tag_length = aead_tag_length_;
    auto out_span = out_plaintext->GetWritableSpan();
    if (EVP_AEAD_CTX_open(raw, out_span.GetStart(), &out_length, out_span.GetLength(), nonce, read_secret_.iv_.size(),
        ciphertext.GetStart(), ciphertext.GetLength(), associated_data.GetStart(), associated_data.GetLength()) != 1) {
        common::LOG_ERROR("EVP_AEAD_CTX_open failed");
        return Result::kDecryptFailed;
    }
    out_plaintext->MoveWritePt(out_length);
    return Result::kOk;
}

ICryptographer::Result AeadBaseCryptographer::EncryptPacket(uint64_t pkt_number, common::BufferSpan& associated_data, common::BufferSpan& plaintext,
    std::shared_ptr<common::IBuffer> out_ciphertext) {
    if (write_secret_.key_.empty() || write_secret_.iv_.empty()) {
        common::LOG_ERROR("encrypt packet but not install secret");
        return Result::kNotInitialized;
    }

    // get nonce
    uint8_t nonce[kPacketNonceLength] = {0};
    MakePacketNonce(nonce, write_secret_.iv_, pkt_number);

    // encrypt
    EVP_AEAD_CTX* raw = write_aead_ctx_.get();
    if (!raw) {
        write_aead_ctx_.reset(EVP_AEAD_CTX_new(aead_, write_secret_.key_.data(), write_secret_.key_.size(), aead_tag_length_));
        raw = write_aead_ctx_.get();
    }
    if (!raw) { 
        common::LOG_ERROR("EVP_AEAD_CTX_new failed");
        return Result::kInternalError;
    }

    size_t out_length = 0;
    auto out_span = out_ciphertext->GetWritableSpan();
    if (EVP_AEAD_CTX_seal(raw, out_span.GetStart(), &out_length, out_span.GetLength(), nonce, write_secret_.iv_.size(),
        plaintext.GetStart(), plaintext.GetLength(), associated_data.GetStart(), associated_data.GetLength()) != 1) {
        common::LOG_ERROR("EVP_AEAD_CTX_seal failed");
        return Result::kEncryptFailed;
    }
    out_ciphertext->MoveWritePt(out_length);
    return Result::kOk;
}

ICryptographer::Result AeadBaseCryptographer::DecryptHeader(common::BufferSpan& ciphertext, common::BufferSpan& sample, uint8_t pn_offset,
    uint8_t& out_packet_num_len, bool is_short) {
    if (read_secret_.hp_.empty()) {
        common::LOG_ERROR("decrypt header but not install hp secret");
        return Result::kNotInitialized;
    }

    // get mask 
    uint8_t mask[kHeaderProtectMaskLength] = {0};
    size_t mask_length = 0;
    if (!MakeHeaderProtectMask(sample, read_secret_.hp_, mask, kHeaderProtectMaskLength, mask_length)) {
        common::LOG_ERROR("make header protect mask failed");
        return Result::kHpFailed;
    }
    if (mask_length < kHeaderProtectMaskLength) {
        common::LOG_ERROR("make header protect mask too short");
        return Result::kHpFailed;
    }

    // remove protection for first byte
    uint8_t* pos = ciphertext.GetStart();
    if (is_short) {
        *pos = *pos ^ (mask[0] & 0x1f);

    } else {
        *pos = *pos ^ (mask[0] & 0x0f);
    }

    // get length of packet number
    out_packet_num_len = (*pos & 0x03);
    
    // remove protection for packet number
    uint8_t* pkt_number_pos = pos + pn_offset;
    for (size_t i = 0; i < out_packet_num_len; i++) {
        *(pkt_number_pos + i) = pkt_number_pos[i] ^ mask[i + 1];
    }

    return Result::kOk;
}

ICryptographer::Result AeadBaseCryptographer::EncryptHeader(common::BufferSpan& plaintext, common::BufferSpan& sample, uint8_t pn_offset,
    size_t pkt_number_len, bool is_short) {
    if (write_secret_.hp_.empty()) {
        common::LOG_ERROR("encrypt header but not install hp secret");
        return Result::kNotInitialized;
    }
    
    // get mask 
    uint8_t mask[kHeaderProtectMaskLength] = {0};
    size_t mask_length = 0;
    if (!MakeHeaderProtectMask(sample, write_secret_.hp_, mask, kHeaderProtectMaskLength, mask_length)) {
        common::LOG_ERROR("make header protect mask failed");
        return Result::kHpFailed;
    }
    if (mask_length < kHeaderProtectMaskLength) {
        common::LOG_ERROR("make header protect mask too short");
        return Result::kHpFailed;
    }

    // protect the first byte of header 
    uint8_t* pos = plaintext.GetStart();
    if (is_short) {
        *pos = *pos ^ (mask[0] & 0x1f);

    } else {
        *pos = *pos ^ (mask[0] & 0x0f);
    }

    // protect packet number
    uint8_t* pkt_number_pos = pos + pn_offset;
    for (size_t i = 0; i < pkt_number_len; i++) {
        *(pkt_number_pos + i) ^= mask[i + 1];
    }
    return Result::kOk;
}

bool AeadBaseCryptographer::MakeHeaderProtectMask(common::BufferSpan& sample, std::vector<uint8_t>& key,
    uint8_t* out_mask, size_t mask_cap, size_t& out_mask_length) {
    out_mask_length = 0;
    if (mask_cap < kHeaderProtectMaskLength) return false;

    // Decide HP algorithm based on AEAD key length (AES-GCM uses AES-ECB for HP; ChaCha20-Poly1305 uses ChaCha20 stream)
    if (aead_key_length_ == 16 || aead_key_length_ == 32) {
        // AES-ECB: mask = AES-ECB(hp_key, sample[16])[0..4]
        const EVP_CIPHER* hp_ecb = (aead_key_length_ == 16) ? EVP_aes_128_ecb() : EVP_aes_256_ecb();
        EVPCIPHERCTXPtr tmp(EVP_CIPHER_CTX_new());
        if (!tmp)
            return false;
        if (EVP_EncryptInit_ex(tmp.get(), hp_ecb, nullptr, key.data(), nullptr) != 1)
            return false;
        EVP_CIPHER_CTX_set_padding(tmp.get(), 0);
        uint8_t block_out[16] = {0};
        int outlen = 0;
        if (EVP_EncryptUpdate(tmp.get(), block_out, &outlen, sample.GetStart(), 16) != 1)
            return false;
        int fin = 0;
        if (EVP_EncryptFinal_ex(tmp.get(), block_out + outlen, &fin) != 1)
            return false;
        // Take first 5 bytes
        memcpy(out_mask, block_out, kHeaderProtectMaskLength);
        out_mask_length = kHeaderProtectMaskLength;
        return true;
    } else {
        // ChaCha20: mask = ChaCha20(key=hp, nonce=sample[0..11], counter=0)[0..4]
        EVPCIPHERCTXPtr tmp(EVP_CIPHER_CTX_new());
        if (!tmp) return false;
        uint8_t iv[16] = {0};
        // Counter (first 4 bytes) = 0 (little-endian)
        // Nonce (next 12 bytes) = sample[0..11]
        memcpy(iv + 4, sample.GetStart(), 12);
        // BoringSSL provides ChaCha20 via EVP_chacha20(). If unavailable in headers, fall back to EVP_aead_chacha20_poly1305 for stream.
        const EVP_CIPHER* chacha = nullptr;
#ifdef EVP_chacha20
        chacha = EVP_chacha20();
#endif
        if (!chacha) {
            // Not available at compile time; cannot produce mask deterministically here
            return false;
        }
        if (EVP_EncryptInit_ex(tmp.get(), chacha, nullptr, key.data(), iv) != 1) return false;
        int outlen = 0;
        if (EVP_EncryptUpdate(tmp.get(), out_mask, &outlen, kHeaderMask.data(), kHeaderMask.size()) != 1) return false;
        out_mask_length = static_cast<size_t>(outlen);
        return out_mask_length >= kHeaderProtectMaskLength;
    }
}

void AeadBaseCryptographer::MakePacketNonce(uint8_t* nonce, std::vector<uint8_t>& iv, uint64_t pkt_number) {
    memcpy(nonce, iv.data(), iv.size());
    // Convert packet number to big-endian as per RFC 9001
    uint64_t be_pn =
        ((pkt_number & 0x00000000000000FFull) << 56) |
        ((pkt_number & 0x000000000000FF00ull) << 40) |
        ((pkt_number & 0x0000000000FF0000ull) << 24) |
        ((pkt_number & 0x00000000FF000000ull) << 8)  |
        ((pkt_number & 0x000000FF00000000ull) >> 8)  |
        ((pkt_number & 0x0000FF0000000000ull) >> 24) |
        ((pkt_number & 0x00FF000000000000ull) >> 40) |
        ((pkt_number & 0xFF00000000000000ull) >> 56);

    // nonce is formed by combining the packet protection IV with the packet number
    for (size_t i = 0; i < 8; ++i) {
        nonce[iv.size() - 8 + i] ^= ((uint8_t *)&be_pn)[i];
    }
}

void AeadBaseCryptographer::CleanSecret(Secret& s) {
    if (!s.key_.empty()) OPENSSL_cleanse(s.key_.data(), s.key_.size());
    if (!s.iv_.empty()) OPENSSL_cleanse(s.iv_.data(), s.iv_.size());
    if (!s.hp_.empty()) OPENSSL_cleanse(s.hp_.data(), s.hp_.size());
    s.key_.clear(); s.iv_.clear(); s.hp_.clear();
}

}
}
