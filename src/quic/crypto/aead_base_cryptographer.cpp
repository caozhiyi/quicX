#include <cstring>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#endif
#include <openssl/aead.h>
#include <openssl/evp.h>
#include "common/log/log.h"
#include "quic/crypto/type.h"
#include "quic/crypto/hkdf.h"
#include "common/decode/decode.h"
#include "common/buffer/if_buffer.h"
#include "quic/packet/packet_number.h"
#include "common/buffer/buffer_read_view.h"
#include "quic/crypto/aead_base_cryptographer.h"

namespace quicx {
namespace quic {

AeadBaseCryptographer::AeadBaseCryptographer():
    digest_(nullptr),
    aead_(nullptr),
    cipher_(nullptr) {

}

AeadBaseCryptographer::~AeadBaseCryptographer() {

}

bool AeadBaseCryptographer::InstallSecret(const uint8_t* secret, uint32_t secret_len, bool is_write) {
    Secret& dest_secret = is_write ? write_secret_ : read_secret_;
    
    // make packet protect key
    dest_secret.key_.resize(aead_key_length_);
    size_t len = 0;
    if (!Hkdf::HkdfExpand(dest_secret.key_.data(), aead_key_length_,  secret, secret_len, kTlsLabelKey, sizeof(kTlsLabelKey) - 1, digest_)) {
        return false;
    }

    // make packet protect iv
    dest_secret.iv_.resize(aead_iv_length_);
    if (!Hkdf::HkdfExpand(dest_secret.iv_.data(), aead_iv_length_,  secret, secret_len, kTlsLabelIv, sizeof(kTlsLabelIv) - 1, digest_)) {
        return false;
    }

    // make header protext key
    dest_secret.hp_.resize(cipher_key_length_);
    if (!Hkdf::HkdfExpand(dest_secret.hp_.data(), cipher_key_length_,  secret, secret_len, kTlsLabelHp, sizeof(kTlsLabelHp) - 1, digest_)) {
        return false;
    }
    return true;
}

bool AeadBaseCryptographer::InstallInitSecret(const uint8_t* secret, uint32_t secret_len, const uint8_t *salt, size_t saltlen, bool is_server) {
    const EVP_MD *digest = EVP_sha256();

    // make init secret
    uint8_t init_secret[kMaxInitSecretLength] = {0};
    if (!Hkdf::HkdfExtract(init_secret, kMaxInitSecretLength, secret, secret_len, salt, saltlen, digest)) {
        return false;
    }

    const uint8_t* read_label = is_server ? kTlsLabelClient : kTlsLabelServer;
    const uint8_t* write_label = is_server ? kTlsLabelServer : kTlsLabelClient;

    uint8_t init_read_secret[kMaxInitSecretLength] = {0};
    if (!Hkdf::HkdfExpand(init_read_secret, kMaxInitSecretLength,  init_secret,
        kMaxInitSecretLength, read_label, sizeof(read_label) - 1, digest)) {
        return false;
    }

    uint8_t init_write_secret[kMaxInitSecretLength] = {0};
    if (!Hkdf::HkdfExpand(init_write_secret, kMaxInitSecretLength,  init_secret,
        kMaxInitSecretLength, write_label, sizeof(write_label) - 1, digest)) {
        return false;
    }

    if (!InstallSecret(init_read_secret, kMaxInitSecretLength, false)) {
        return false;
    }

    if (!InstallSecret(init_write_secret, kMaxInitSecretLength, true)) {
        return false;
    }

    return true;
}

bool AeadBaseCryptographer::DecryptPacket(uint64_t pkt_number, common::BufferSpan& associated_data, common::BufferSpan& ciphertext,
    std::shared_ptr<common::IBufferWrite> out_plaintext) {
    if (read_secret_.key_.empty() || read_secret_.iv_.empty()) {
        common::LOG_ERROR("decrypt packet but not install secret");
        return false;
    }

    // get nonce
    uint8_t nonce[kPacketNonceLength] = {0};
    MakePacketNonce(nonce, read_secret_.iv_, pkt_number);

    // encrypt
    EVPAEADCTXPtr ctx = EVP_AEAD_CTX_new(aead_, read_secret_.key_.data(), read_secret_.key_.size(), aead_tag_length_);
    if (!ctx) {
        common::LOG_ERROR("EVP_AEAD_CTX_new failed");
        return false;
    }

    size_t out_length = 0;
    auto tag_length = aead_tag_length_;
    auto out_span = out_plaintext->GetWriteSpan();
    if (EVP_AEAD_CTX_open(ctx.get(), out_span.GetStart(), &out_length, out_span.GetLength(), nonce, read_secret_.iv_.size(),
        ciphertext.GetStart(), ciphertext.GetLength(), associated_data.GetStart(), associated_data.GetLength()) != 1) {
        common::LOG_ERROR("EVP_AEAD_CTX_open failed");
        return false;
    }
    out_plaintext->MoveWritePt(out_length);
    return true;
}

bool AeadBaseCryptographer::EncryptPacket(uint64_t pkt_number, common::BufferSpan& associated_data, common::BufferSpan& plaintext,
    std::shared_ptr<common::IBufferWrite> out_ciphertext) {
    if (write_secret_.key_.empty() || write_secret_.iv_.empty()) {
        common::LOG_ERROR("encrypt packet but not install secret");
        return false;
    }

    // get nonce
    uint8_t nonce[kPacketNonceLength] = {0};
    MakePacketNonce(nonce, write_secret_.iv_, pkt_number);

    // encrypt
    EVPAEADCTXPtr ctx = EVP_AEAD_CTX_new(aead_, write_secret_.key_.data(), write_secret_.key_.size(), aead_tag_length_);
    if (!ctx) {
        common::LOG_ERROR("EVP_AEAD_CTX_new failed");
        return false;
    }

    size_t out_length = 0;
    auto out_span = out_ciphertext->GetWriteSpan();
    if (EVP_AEAD_CTX_seal(ctx.get(), out_span.GetStart(), &out_length, out_span.GetLength(), nonce, write_secret_.iv_.size(),
        plaintext.GetStart(), plaintext.GetLength(), associated_data.GetStart(), associated_data.GetLength()) != 1) {
        common::LOG_ERROR("EVP_AEAD_CTX_seal failed");
        return false;
    }
    out_ciphertext->MoveWritePt(out_length);
    return true;
}

bool AeadBaseCryptographer::DecryptHeader(common::BufferSpan& ciphertext, common::BufferSpan& sample, uint8_t pn_offset,
    uint8_t& out_packet_num_len, bool is_short) {
    if (read_secret_.hp_.empty()) {
        common::LOG_ERROR("decrypt header but not install hp secret");
        return false;
    }

    // get mask 
    uint8_t mask[kHeaderProtectMaskLength] = {0};
    size_t mask_length = 0;
    if (!MakeHeaderProtectMask(sample, read_secret_.hp_, mask, kHeaderProtectMaskLength, mask_length)) {
        common::LOG_ERROR("make header protect mask failed");
        return false;
    }
    if (mask_length < kHeaderProtectMaskLength) {
        common::LOG_ERROR("make header protect mask too short");
        return false;
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

    return true;
}

bool AeadBaseCryptographer::EncryptHeader(common::BufferSpan& plaintext, common::BufferSpan& sample, uint8_t pn_offset,
    size_t pkt_number_len, bool is_short) {
    if (write_secret_.hp_.empty()) {
        common::LOG_ERROR("encrypt header but not install hp secret");
        return false;
    }
    
    // get mask 
    uint8_t mask[kHeaderProtectMaskLength] = {0};
    size_t mask_length = 0;
    if (!MakeHeaderProtectMask(sample, write_secret_.hp_, mask, kHeaderProtectMaskLength, mask_length)) {
        common::LOG_ERROR("make header protect mask failed");
        return false;
    }
    if (mask_length < kHeaderProtectMaskLength) {
        common::LOG_ERROR("make header protect mask too short");
        return false;
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
    return true;
}

bool AeadBaseCryptographer::MakeHeaderProtectMask(common::BufferSpan& sample, std::vector<uint8_t>& key,
    uint8_t* out_mask, size_t mask_cap, size_t& out_mask_length) {
    out_mask_length = 0;
    
    EVPCIPHERCTXPtr ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        common::LOG_ERROR("create evp cipher ctx failed");
        return false;
    }

    if (EVP_EncryptInit_ex(ctx.get(), cipher_, NULL, key.data(), sample.GetStart()) != 1) {
        common::LOG_ERROR("EVP_EncryptInit_ex failed");
        return false;
    }

    int len = 0;
    if (EVP_EncryptUpdate(ctx.get(), out_mask, &len, kHeaderMask, sizeof(kHeaderMask) - 1) != 1) {
        common::LOG_ERROR("EVP_EncryptUpdate failed");
        return false;
    }
    out_mask_length = len;

    if (EVP_EncryptFinal_ex(ctx.get(), out_mask + out_mask_length, &len) != 1) {
        common::LOG_ERROR("EVP_EncryptFinal_ex failed");
        return false;
    }

    if (len != 0) {
        common::LOG_ERROR("EVP_EncryptFinal_ex out put length is not zero");
        return false;
    }

    return true;
}

void AeadBaseCryptographer::MakePacketNonce(uint8_t* nonce, std::vector<uint8_t>& iv, uint64_t pkt_number) {
    memcpy(nonce, iv.data(), iv.size());
    pkt_number = PktNumberN2L(pkt_number);

    // nonce is formed by combining the packet protection IV with the packet number
    for (size_t i = 0; i < 8; ++i) {
        nonce[iv.size() - 8 + i] ^= ((uint8_t *)&pkt_number)[i];
    }
}

uint64_t AeadBaseCryptographer::PktNumberN2L(uint64_t pkt_number) {
    return ((uint64_t)(ntohl((uint32_t)(pkt_number))) << 32 | ntohl((uint32_t)((pkt_number) >> 32)));
}

}
}
