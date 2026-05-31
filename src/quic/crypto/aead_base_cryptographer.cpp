#include <cstring>

#include <openssl/aead.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include "common/log/log.h"
#include "quic/crypto/aead_base_cryptographer.h"
#include "quic/crypto/hkdf.h"
#include "quic/crypto/type.h"

namespace quicx {
namespace quic {

AeadBaseCryptographer::AeadBaseCryptographer():
    digest_(nullptr),
    aead_(nullptr),
    cipher_(nullptr) {}

AeadBaseCryptographer::~AeadBaseCryptographer() {
    CleanSecret(read_secret_);
    CleanSecret(write_secret_);
    CleanSecret(prev_read_secret_);
    if (!raw_read_secret_.empty()) OPENSSL_cleanse(raw_read_secret_.data(), raw_read_secret_.size());
    if (!raw_write_secret_.empty()) OPENSSL_cleanse(raw_write_secret_.data(), raw_write_secret_.size());
    raw_read_secret_.clear();
    raw_write_secret_.clear();
}

ICryptographer::Result AeadBaseCryptographer::InstallSecret(const uint8_t* secret, size_t secret_len, bool is_write) {
    // Default to using stored version
    return InstallSecretWithVersion(secret, secret_len, is_write, quic_version_);
}

ICryptographer::Result AeadBaseCryptographer::InstallSecretWithVersion(
    const uint8_t* secret, size_t secret_len, bool is_write, uint32_t version) {
    Secret& dest_secret = is_write ? write_secret_ : read_secret_;

    // RFC 9001 §6: Save the raw traffic secret for Key Update derivation
    auto& raw_secret = is_write ? raw_write_secret_ : raw_read_secret_;
    raw_secret.assign(secret, secret + secret_len);

    // Get version-specific labels
    QuicLabels labels = GetQuicLabels(version);

    // make packet protect key
    dest_secret.key_.resize(aead_key_length_);
    if (!Hkdf::HkdfExpand(dest_secret.key_.data(), aead_key_length_, secret, secret_len,
            labels.key, labels.key_len, digest_)) {
        CleanSecret(dest_secret);
        return Result::kDeriveFailed;
    }

    // make packet protect iv
    dest_secret.iv_.resize(aead_iv_length_);
    if (!Hkdf::HkdfExpand(dest_secret.iv_.data(), aead_iv_length_, secret, secret_len,
            labels.iv, labels.iv_len, digest_)) {
        CleanSecret(dest_secret);
        return Result::kDeriveFailed;
    }

    // make header protect key
    dest_secret.hp_.resize(cipher_key_length_);
    if (!Hkdf::HkdfExpand(dest_secret.hp_.data(), cipher_key_length_, secret, secret_len,
            labels.hp, labels.hp_len, digest_)) {
        CleanSecret(dest_secret);
        return Result::kDeriveFailed;
    }
    
    // Initialize or refresh cached contexts
    //
    // PERF (P0): the HP context is initialized ONCE per key install. We pick
    // ECB for AES (matches what MakeHeaderProtectMask does on the fast path,
    // so each subsequent EVP_EncryptUpdate just runs one block of AES with
    // the already-scheduled key). For ChaCha20 we keep the legacy CTR-style
    // init via cipher_, since the ChaCha20 subclass overrides
    // MakeHeaderProtectMask and does not consume hp_*_ctx_ on the hot path.
    const EVP_CIPHER* hp_cipher = nullptr;
    if (aead_key_length_ == 16) {
        hp_cipher = EVP_aes_128_ecb();
    } else if (aead_key_length_ == 32 && cipher_ == EVP_aes_256_ctr()) {
        hp_cipher = EVP_aes_256_ecb();
    } else {
        // ChaCha20-Poly1305 (or any non-AES-GCM future cipher): retain legacy
        // ctx init using the per-class `cipher_` so we don't crash if the
        // base-class HP path is ever hit.
        hp_cipher = cipher_;
    }
    if (is_write) {
        write_aead_ctx_.reset(
            EVP_AEAD_CTX_new(aead_, dest_secret.key_.data(), dest_secret.key_.size(), aead_tag_length_));
        hp_write_ctx_.reset(EVP_CIPHER_CTX_new());
        if (hp_write_ctx_.get() && hp_cipher) {
            // For ECB: pass NULL iv (ECB has no IV). For CTR / ChaCha20 fallback,
            // pass kHeaderMask as the IV (kept for backward compatibility with
            // any code path that might use it).
            const uint8_t* hp_iv = (hp_cipher == EVP_aes_128_ecb() || hp_cipher == EVP_aes_256_ecb())
                                       ? nullptr
                                       : kHeaderMask.data();
            EVP_EncryptInit_ex(hp_write_ctx_.get(), hp_cipher, NULL, dest_secret.hp_.data(), hp_iv);
            if (hp_cipher == EVP_aes_128_ecb() || hp_cipher == EVP_aes_256_ecb()) {
                EVP_CIPHER_CTX_set_padding(hp_write_ctx_.get(), 0);
            }
        }
    } else {
        read_aead_ctx_.reset(
            EVP_AEAD_CTX_new(aead_, dest_secret.key_.data(), dest_secret.key_.size(), aead_tag_length_));
        hp_read_ctx_.reset(EVP_CIPHER_CTX_new());
        if (hp_read_ctx_.get() && hp_cipher) {
            const uint8_t* hp_iv = (hp_cipher == EVP_aes_128_ecb() || hp_cipher == EVP_aes_256_ecb())
                                       ? nullptr
                                       : kHeaderMask.data();
            EVP_EncryptInit_ex(hp_read_ctx_.get(), hp_cipher, NULL, dest_secret.hp_.data(), hp_iv);
            if (hp_cipher == EVP_aes_128_ecb() || hp_cipher == EVP_aes_256_ecb()) {
                EVP_CIPHER_CTX_set_padding(hp_read_ctx_.get(), 0);
            }
        }
    }
    return Result::kOk;
}

ICryptographer::Result AeadBaseCryptographer::KeyUpdate(
    const uint8_t* new_base_secret, size_t secret_len, bool update_write) {
    // Use stored version
    return KeyUpdateWithVersion(new_base_secret, secret_len, update_write, quic_version_);
}

ICryptographer::Result AeadBaseCryptographer::KeyUpdateWithVersion(
    const uint8_t* new_base_secret, size_t secret_len, bool update_write, uint32_t version) {
    // RFC 9001 §6: next_secret = HKDF-Expand-Label(current_secret, "tls13 quic ku", "", hash_len)
    // RFC 9369: For v2, use "tls13 quicv2 ku" label
    // IMPORTANT: current_secret is the raw TLS traffic secret, NOT the derived AEAD key
    const auto& raw_secret = update_write ? raw_write_secret_ : raw_read_secret_;
    std::vector<uint8_t> base;
    if (new_base_secret && secret_len > 0) {
        base.assign(new_base_secret, new_base_secret + secret_len);
    } else {
        if (raw_secret.empty()) {
            LOG_ERROR("KeyUpdate: no raw traffic secret available");
            return Result::kNotInitialized;
        }
        base = raw_secret;
    }
    
    // Get version-specific labels
    QuicLabels labels = GetQuicLabels(version);
    
    size_t hash_len = EVP_MD_size(digest_);
    std::vector<uint8_t> next_secret(hash_len);
    if (!Hkdf::HkdfExpand(next_secret.data(), next_secret.size(), base.data(), base.size(),
            labels.ku, labels.ku_len, digest_)) {
        OPENSSL_cleanse(base.data(), base.size());
        return Result::kDeriveFailed;
    }

    // RFC 9001 §6: "The header protection key is not updated."
    // Save current HP key and context BEFORE any modifications
    Secret& current_secret = update_write ? write_secret_ : read_secret_;
    std::vector<uint8_t> saved_hp = current_secret.hp_;
    EVPCIPHERCTXPtr& hp_ctx_ref = update_write ? hp_write_ctx_ : hp_read_ctx_;
    EVPCIPHERCTXPtr saved_hp_ctx = std::move(hp_ctx_ref);

    // RFC 9001 §6: Before updating read keys, save current read key as previous
    // so that reordered packets encrypted with the old key can still be decrypted
    if (!update_write) {
        CleanSecret(prev_read_secret_);
        prev_read_secret_ = read_secret_;
        // Transfer ownership of read AEAD ctx to prev
        prev_read_aead_ctx_ = std::move(read_aead_ctx_);
        // Don't clean read_secret_ here - InstallSecretWithVersion will overwrite it
        read_secret_ = Secret();  // Reset without cleansing (data moved to prev)
    }

    // Reinstall secrets using next_secret (this derives new key, iv, AND hp)
    auto r = InstallSecretWithVersion(next_secret.data(), next_secret.size(), update_write, version);

    // Restore the original HP key and context (HP is not updated during Key Update)
    if (r == Result::kOk && !saved_hp.empty()) {
        Secret& updated = update_write ? write_secret_ : read_secret_;
        updated.hp_ = std::move(saved_hp);
        hp_ctx_ref = std::move(saved_hp_ctx);
    }

    // Clear temporary key material
    OPENSSL_cleanse(base.data(), base.size());
    OPENSSL_cleanse(next_secret.data(), next_secret.size());

    if (r == Result::kOk) key_updated_flag_ = true;
    return r;
}

ICryptographer::Result AeadBaseCryptographer::InstallInitSecret(
    const uint8_t* secret, size_t secret_len, const uint8_t* salt, size_t saltlen, bool is_server) {
    const EVP_MD* digest = EVP_sha256();

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

    if (!is_server) {
        std::swap(read_label, write_label);
        std::swap(read_label_len, write_label_len);
    }

    uint8_t init_read_secret[kMaxInitSecretLength] = {0};
    if (!Hkdf::HkdfExpand(init_read_secret, kMaxInitSecretLength, init_secret, kMaxInitSecretLength, read_label,
            read_label_len, digest)) {
        return Result::kDeriveFailed;
    }

    uint8_t init_write_secret[kMaxInitSecretLength] = {0};
    if (!Hkdf::HkdfExpand(init_write_secret, kMaxInitSecretLength, init_secret, kMaxInitSecretLength, write_label,
            write_label_len, digest)) {
        return Result::kDeriveFailed;
    }

    // Use version-aware secret installation (default labels for Initial are same in v1/v2)
    if (InstallSecretWithVersion(init_read_secret, kMaxInitSecretLength, false, quic_version_) != Result::kOk) {
        OPENSSL_cleanse(init_secret, sizeof(init_secret));
        OPENSSL_cleanse(init_read_secret, sizeof(init_read_secret));
        OPENSSL_cleanse(init_write_secret, sizeof(init_write_secret));
        return Result::kDeriveFailed;
    }

    if (InstallSecretWithVersion(init_write_secret, kMaxInitSecretLength, true, quic_version_) != Result::kOk) {
        OPENSSL_cleanse(init_secret, sizeof(init_secret));
        OPENSSL_cleanse(init_read_secret, sizeof(init_read_secret));
        OPENSSL_cleanse(init_write_secret, sizeof(init_write_secret));
        return Result::kDeriveFailed;
    }

    // Clear temporary key material from stack
    OPENSSL_cleanse(init_secret, sizeof(init_secret));
    OPENSSL_cleanse(init_read_secret, sizeof(init_read_secret));
    OPENSSL_cleanse(init_write_secret, sizeof(init_write_secret));

    return Result::kOk;
}

ICryptographer::Result AeadBaseCryptographer::InstallInitSecretWithVersion(
    const uint8_t* secret, size_t secret_len, uint32_t version, bool is_server) {
    // Store version for future operations
    quic_version_ = version;
    
    // Get version-specific salt
    const uint8_t* salt = GetInitialSalt(version);
    size_t salt_len = GetInitialSaltLength(version);
    
    LOG_INFO("Installing Initial secret with version 0x%08x", version);
    
    return InstallInitSecret(secret, secret_len, salt, salt_len, is_server);
}

ICryptographer::Result AeadBaseCryptographer::DecryptPacket(uint64_t pkt_number, common::BufferSpan& associated_data,
    common::BufferSpan& ciphertext, std::shared_ptr<common::IBuffer> out_plaintext) {
    if (read_secret_.key_.empty() || read_secret_.iv_.empty()) {
        LOG_ERROR("decrypt packet but not install secret");
        return Result::kNotInitialized;
    }

    // get nonce
    uint8_t nonce[kPacketNonceLength] = {0};
    MakePacketNonce(nonce, read_secret_.iv_, pkt_number);

    // encrypt
    EVP_AEAD_CTX* raw = read_aead_ctx_.get();
    if (!raw) {
        read_aead_ctx_.reset(
            EVP_AEAD_CTX_new(aead_, read_secret_.key_.data(), read_secret_.key_.size(), aead_tag_length_));
        raw = read_aead_ctx_.get();
    }
    if (!raw) {
        LOG_ERROR("EVP_AEAD_CTX_new failed");
        return Result::kInternalError;
    }

    size_t out_length = 0;
    auto tag_length = aead_tag_length_;
    auto out_span = out_plaintext->GetWritableSpan();

    if (EVP_AEAD_CTX_open(raw, out_span.GetStart(), &out_length, out_span.GetLength(), nonce, read_secret_.iv_.size(),
            ciphertext.GetStart(), ciphertext.GetLength(), associated_data.GetStart(),
            associated_data.GetLength()) != 1) {
        LOG_ERROR("EVP_AEAD_CTX_open failed");
        return Result::kDecryptFailed;
    }
    out_plaintext->MoveWritePt(out_length);
    return Result::kOk;
}

ICryptographer::Result AeadBaseCryptographer::DecryptPacketWithPrevKey(uint64_t pkt_number, common::BufferSpan& associated_data,
    common::BufferSpan& ciphertext, std::shared_ptr<common::IBuffer> out_plaintext) {
    if (prev_read_secret_.key_.empty() || prev_read_secret_.iv_.empty()) {
        return Result::kNotInitialized;
    }

    // get nonce using previous IV
    uint8_t nonce[kPacketNonceLength] = {0};
    MakePacketNonce(nonce, prev_read_secret_.iv_, pkt_number);

    // use previous read AEAD context
    EVP_AEAD_CTX* raw = prev_read_aead_ctx_.get();
    if (!raw) {
        prev_read_aead_ctx_.reset(
            EVP_AEAD_CTX_new(aead_, prev_read_secret_.key_.data(), prev_read_secret_.key_.size(), aead_tag_length_));
        raw = prev_read_aead_ctx_.get();
    }
    if (!raw) {
        return Result::kInternalError;
    }

    size_t out_length = 0;
    auto out_span = out_plaintext->GetWritableSpan();

    if (EVP_AEAD_CTX_open(raw, out_span.GetStart(), &out_length, out_span.GetLength(), nonce, prev_read_secret_.iv_.size(),
            ciphertext.GetStart(), ciphertext.GetLength(), associated_data.GetStart(),
            associated_data.GetLength()) != 1) {
        return Result::kDecryptFailed;
    }
    out_plaintext->MoveWritePt(out_length);
    return Result::kOk;
}

ICryptographer::Result AeadBaseCryptographer::EncryptPacket(uint64_t pkt_number, common::BufferSpan& associated_data,
    common::BufferSpan& plaintext, std::shared_ptr<common::IBuffer> out_ciphertext) {
    if (write_secret_.key_.empty() || write_secret_.iv_.empty()) {
        LOG_ERROR("encrypt packet but not install secret");
        return Result::kNotInitialized;
    }

    // get nonce
    uint8_t nonce[kPacketNonceLength] = {0};
    MakePacketNonce(nonce, write_secret_.iv_, pkt_number);

    // encrypt
    EVP_AEAD_CTX* raw = write_aead_ctx_.get();
    if (!raw) {
        write_aead_ctx_.reset(
            EVP_AEAD_CTX_new(aead_, write_secret_.key_.data(), write_secret_.key_.size(), aead_tag_length_));
        raw = write_aead_ctx_.get();
    }
    if (!raw) {
        LOG_ERROR("EVP_AEAD_CTX_new failed");
        return Result::kInternalError;
    }

    size_t out_length = 0;
    auto out_span = out_ciphertext->GetWritableSpan();
    if (EVP_AEAD_CTX_seal(raw, out_span.GetStart(), &out_length, out_span.GetLength(), nonce, write_secret_.iv_.size(),
            plaintext.GetStart(), plaintext.GetLength(), associated_data.GetStart(),
            associated_data.GetLength()) != 1) {
        LOG_ERROR("EVP_AEAD_CTX_seal failed");
        return Result::kEncryptFailed;
    }
    out_ciphertext->MoveWritePt(out_length);
    return Result::kOk;
}

ICryptographer::Result AeadBaseCryptographer::DecryptHeader(common::BufferSpan& ciphertext, common::BufferSpan& sample,
    uint8_t pn_offset, uint8_t& out_packet_num_len, bool is_short) {
    if (read_secret_.hp_.empty()) {
        LOG_ERROR("decrypt header but not install hp secret");
        return Result::kNotInitialized;
    }

    // get mask
    uint8_t mask[kHeaderProtectMaskLength] = {0};
    size_t mask_length = 0;
    if (!MakeHeaderProtectMask(sample, read_secret_.hp_, mask, kHeaderProtectMaskLength, mask_length,
            hp_read_ctx_.get())) {
        LOG_ERROR("make header protect mask failed");
        return Result::kHpFailed;
    }
    if (mask_length < kHeaderProtectMaskLength) {
        LOG_ERROR("make header protect mask too short");
        return Result::kHpFailed;
    }

    // remove protection for first byte
    uint8_t* pos = ciphertext.GetStart();
    uint8_t old_flag = *pos;
    if (is_short) {
        *pos = *pos ^ (mask[0] & 0x1f);

    } else {
        *pos = *pos ^ (mask[0] & 0x0f);
    }
    uint8_t new_flag = *pos;

    // get length of packet number from header flags
    // RFC 9000: The 2-bit field encodes (actual_length - 1)
    uint8_t stored_pn_len = (*pos & 0x03);
    out_packet_num_len = stored_pn_len + 1;  // Convert to actual length

    // remove protection for packet number
    uint8_t* pkt_number_pos = pos + pn_offset;
    for (size_t i = 0; i < out_packet_num_len; i++) {
        *(pkt_number_pos + i) = pkt_number_pos[i] ^ mask[i + 1];
    }

    return Result::kOk;
}

ICryptographer::Result AeadBaseCryptographer::EncryptHeader(common::BufferSpan& plaintext, common::BufferSpan& sample,
    uint8_t pn_offset, size_t pkt_number_len, bool is_short) {
    if (write_secret_.hp_.empty()) {
        LOG_ERROR("encrypt header but not install hp secret");
        return Result::kNotInitialized;
    }

    // get mask
    uint8_t mask[kHeaderProtectMaskLength] = {0};
    size_t mask_length = 0;
    if (!MakeHeaderProtectMask(sample, write_secret_.hp_, mask, kHeaderProtectMaskLength, mask_length,
            hp_write_ctx_.get())) {
        LOG_ERROR("make header protect mask failed");
        return Result::kHpFailed;
    }
    if (mask_length < kHeaderProtectMaskLength) {
        LOG_ERROR("make header protect mask too short");
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
    uint8_t* out_mask, size_t mask_cap, size_t& out_mask_length, EVP_CIPHER_CTX* cached_hp_ctx) {
    out_mask_length = 0;
    if (mask_cap < kHeaderProtectMaskLength) return false;

    // Decide HP algorithm based on AEAD key length (AES-GCM uses AES-ECB for HP; ChaCha20-Poly1305 uses ChaCha20
    // stream)
    if (aead_key_length_ == 16 || aead_key_length_ == 32) {
        // AES-ECB: mask = AES-ECB(hp_key, sample[16])[0..4]
        //
        // PERF (P0): Use the cached EVP_CIPHER_CTX (already initialized with the
        // HP key in InstallSecretWithVersion / KeyUpdate). AES-ECB is stateless
        // per 16-byte block, so EVP_EncryptUpdate on the cached ctx is safe and
        // skips the per-packet EVP_CIPHER_CTX_new() + EVP_EncryptInit_ex()
        // (which re-runs the AES key schedule). Profiling showed this path
        // accounted for ~20% of the client CPU budget at 1Gbps.
        uint8_t block_out[16] = {0};
        int outlen = 0;
        if (cached_hp_ctx) {
            if (EVP_EncryptUpdate(cached_hp_ctx, block_out, &outlen, sample.GetStart(), 16) != 1) return false;
            // Note: deliberately NOT calling EVP_EncryptFinal_ex here. Doing so
            // would emit padding (or fail with padding disabled); we treat ECB
            // as a one-shot 16->16 transform. Padding is already disabled when
            // the ctx was initialized in InstallSecretWithVersion.
            memcpy(out_mask, block_out, kHeaderProtectMaskLength);
            out_mask_length = kHeaderProtectMaskLength;
            return true;
        }

        // Slow path: no cached ctx (e.g. unit test, or before secret install).
        // Allocate a one-shot ctx as before.
        const EVP_CIPHER* hp_ecb = (aead_key_length_ == 16) ? EVP_aes_128_ecb() : EVP_aes_256_ecb();
        EVPCIPHERCTXPtr tmp(EVP_CIPHER_CTX_new());
        if (!tmp) return false;
        if (EVP_EncryptInit_ex(tmp.get(), hp_ecb, nullptr, key.data(), nullptr) != 1) return false;
        EVP_CIPHER_CTX_set_padding(tmp.get(), 0);
        if (EVP_EncryptUpdate(tmp.get(), block_out, &outlen, sample.GetStart(), 16) != 1) return false;
        int fin = 0;
        if (EVP_EncryptFinal_ex(tmp.get(), block_out + outlen, &fin) != 1) return false;
        memcpy(out_mask, block_out, kHeaderProtectMaskLength);
        out_mask_length = kHeaderProtectMaskLength;
        return true;
    } else {
        // ChaCha20: handled by override in ChaCha20Poly1305Cryptographer. The
        // legacy fallback path below is kept in case some future cipher reaches
        // here via the base class.
        EVPCIPHERCTXPtr tmp(EVP_CIPHER_CTX_new());
        if (!tmp) return false;
        uint8_t iv[16] = {0};
        memcpy(iv + 4, sample.GetStart(), 12);
        const EVP_CIPHER* chacha = nullptr;
#ifdef EVP_chacha20
        chacha = EVP_chacha20();
#endif
        if (!chacha) {
            LOG_ERROR("ChaCha20 not available");
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
    uint64_t be_pn = ((pkt_number & 0x00000000000000FFull) << 56) | ((pkt_number & 0x000000000000FF00ull) << 40) |
                     ((pkt_number & 0x0000000000FF0000ull) << 24) | ((pkt_number & 0x00000000FF000000ull) << 8) |
                     ((pkt_number & 0x000000FF00000000ull) >> 8) | ((pkt_number & 0x0000FF0000000000ull) >> 24) |
                     ((pkt_number & 0x00FF000000000000ull) >> 40) | ((pkt_number & 0xFF00000000000000ull) >> 56);

    // nonce is formed by combining the packet protection IV with the packet number
    for (size_t i = 0; i < 8; ++i) {
        nonce[iv.size() - 8 + i] ^= ((uint8_t*)&be_pn)[i];
    }
}

void AeadBaseCryptographer::CleanSecret(Secret& s) {
    if (!s.key_.empty()) OPENSSL_cleanse(s.key_.data(), s.key_.size());
    if (!s.iv_.empty()) OPENSSL_cleanse(s.iv_.data(), s.iv_.size());
    if (!s.hp_.empty()) OPENSSL_cleanse(s.hp_.data(), s.hp_.size());
    s.key_.clear();
    s.iv_.clear();
    s.hp_.clear();
}

}  // namespace quic
}  // namespace quicx
