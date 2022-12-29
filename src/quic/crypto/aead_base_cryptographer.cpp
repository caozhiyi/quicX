#include <cstring>
#include <netinet/in.h>	
#include <openssl/aead.h>
#include <openssl/evp.h>

#include "common/log/log.h"
#include "common/buffer/buffer_interface.h"
#include "quic/crypto/type.h"
#include "quic/crypto/hkdf.h"
#include "quic/crypto/aead_base_cryptographer.h"

namespace quicx {

AeadBaseCryptographer::AeadBaseCryptographer():
    _digest(nullptr),
    _aead(nullptr),
    _cipher(nullptr) {

}

AeadBaseCryptographer::~AeadBaseCryptographer() {
}

bool AeadBaseCryptographer::InstallSecret(uint8_t* secret, uint32_t secret_len, bool is_write) {
    Secret& dest_secret = is_write ? _write_secret : _read_secret;
    
    // make packet protect key
    dest_secret._key.resize(_aead_key_length);
    size_t len = 0;
    if (!Hkdf::HkdfExpand(dest_secret._key.data(), _aead_key_length,  secret, secret_len, __tls_label_key, sizeof(__tls_label_key) - 1, _digest)) {
        return false;
    }

    // make packet protect iv
    dest_secret._iv.resize(_aead_iv_length);
    if (!Hkdf::HkdfExpand(dest_secret._iv.data(), _aead_iv_length,  secret, secret_len, __tls_label_iv, sizeof(__tls_label_iv) - 1, _digest)) {
        return false;
    }

    // make header protext key
    dest_secret._hp.resize(_cipher_key_length);
    if (!Hkdf::HkdfExpand(dest_secret._hp.data(), _cipher_key_length,  secret, secret_len, __tls_label_hp, sizeof(__tls_label_hp) - 1, _digest)) {
        return false;
    }
    return true;
}

bool AeadBaseCryptographer::InstallInitSecret(uint8_t* secret, uint32_t secret_len, const uint8_t *salt, size_t saltlen, bool is_server) {
    const EVP_MD *digest = EVP_sha256();

    // make init secret
    uint8_t init_secret[__max_init_secret_length] = {0};
    if (!Hkdf::HkdfExtract(init_secret, __max_init_secret_length, secret, secret_len, salt, saltlen, digest)) {
        return false;
    }

    const uint8_t* read_label = is_server ? __tls_label_client : __tls_label_server;
    const uint8_t* write_label = is_server ? __tls_label_server : __tls_label_client;

    uint8_t init_read_secret[__max_init_secret_length] = {0};
    if (!Hkdf::HkdfExpand(init_read_secret, __max_init_secret_length,  init_secret,
        __max_init_secret_length, read_label, sizeof(read_label) - 1, digest)) {
        return false;
    }

    uint8_t init_write_secret[__max_init_secret_length] = {0};
    if (!Hkdf::HkdfExpand(init_write_secret, __max_init_secret_length,  init_secret,
        __max_init_secret_length, write_label, sizeof(write_label) - 1, digest)) {
        return false;
    }

    if (!InstallSecret(init_read_secret, __max_init_secret_length, false)) {
        return false;
    }

    if (!InstallSecret(init_write_secret, __max_init_secret_length, true)) {
        return false;
    }

    return true;
}

bool AeadBaseCryptographer::DecryptPacket(uint64_t pkt_number, BufferView associated_data, std::shared_ptr<IBufferReadOnly> ciphertext,
    std::shared_ptr<IBufferReadOnly> out_plaintext) {
    return true;
}

bool AeadBaseCryptographer::EncryptPacket(uint64_t pkt_number, BufferView associated_data, std::shared_ptr<IBufferReadOnly> plaintext,
    std::shared_ptr<IBufferReadOnly> out_ciphertext) {
    if (_write_secret._key.empty() || _write_secret._iv.empty()) {
        LOG_ERROR("encrypt packet but not install secret");
        return false;
    }

    // get nonce
    uint8_t nonce[__packet_nonce_length] = {0};
    MakePacketNonce(nonce, _write_secret._iv, pkt_number);

    // encrypt
    EVPAEADCTXPtr ctx = EVP_AEAD_CTX_new(_aead, _write_secret._key.data(), _write_secret._key.size(), _aead_tag_length);
    if (!ctx) {
        LOG_ERROR("EVP_AEAD_CTX_new failed");
        return false;
    }

    size_t out_length = 0;
    auto out_pair = out_ciphertext->GetWritePair();
    auto in_pair = plaintext->GetWritePair();
    if (EVP_AEAD_CTX_open(ctx.get(), out_pair.first, &out_length, out_pair.second - out_pair.first, nonce, 
        __packet_nonce_length, in_pair.first, plaintext->GetCanReadLength(), associated_data.GetData(), associated_data.GetLength()) != 1) {
        LOG_ERROR("EVP_AEAD_CTX_open failed");
        return false;
    }
    return true;
}

bool AeadBaseCryptographer::DecryptHeader(std::shared_ptr<IBufferReadOnly> ciphertext, uint8_t pn_offset, bool is_short) {
    if (_read_secret._hp.empty()) {
        LOG_ERROR("decrypt header but not install hp secret");
        return false;
    }

    // get sample 
    auto data_pair = ciphertext->GetReadPair();
    uint8_t* pkt_number_pos = data_pair.first + pn_offset;
    BufferView sample = BufferView(pkt_number_pos + 4, __header_protect_sample_length);

    // get mask 
    uint8_t mask[__header_protect_mask_length] = {0};
    size_t mask_length = 0;
    if (!MakeHeaderProtectMask(ciphertext, sample, _read_secret._hp, mask, __header_protect_mask_length, mask_length)) {
        LOG_ERROR("make header protect mask failed");
        return false;
    }
    if (mask_length < __header_protect_mask_length) {
        LOG_ERROR("make header protect mask too short");
        return false;
    }

    // remove protection for first byte
    if (is_short) {
        *data_pair.first = *data_pair.first ^ (mask[0] & 0x1f);

    } else {
        *data_pair.first = *data_pair.first ^ (mask[0] & 0x0f);
    }

    // get length of packet number
    size_t pkt_number_len = (*data_pair.first & 0x03) + 1;
    
    // remove protection for packet number
    for (size_t i = 0; i < pkt_number_len; i++) {
        *(pkt_number_pos + i) = pkt_number_pos[i] ^ mask[i + 1];
    }

    return true;
}

bool AeadBaseCryptographer::EncryptHeader(std::shared_ptr<IBufferReadOnly> plaintext, uint8_t pn_offset, size_t pkt_number_len, bool is_short) {
    if (_write_secret._hp.empty()) {
        LOG_ERROR("encrypt header but not install hp secret");
        return false;
    }

    // get sample 
    auto data_pair = plaintext->GetReadPair();
    uint8_t* pkt_number_pos = data_pair.first + pn_offset;
    BufferView sample = BufferView(pkt_number_pos + 4, __header_protect_sample_length);

    // get mask 
    uint8_t mask[__header_protect_mask_length] = {0};
    size_t mask_length = 0;
    if (!MakeHeaderProtectMask(plaintext, sample, _write_secret._hp, mask, __header_protect_mask_length, mask_length)) {
        LOG_ERROR("make header protect mask failed");
        return false;
    }
    if (mask_length < __header_protect_mask_length) {
        LOG_ERROR("make header protect mask too short");
        return false;
    }

    // protect the first byte of header 
    if (is_short) {
        *data_pair.first = *data_pair.first ^ (mask[0] & 0x1f);

    } else {
        *data_pair.first = *data_pair.first ^ (mask[0] & 0x0f);
    }

    // protect packet number
    for (size_t i = 0; i < pkt_number_len; i++) {
        *(pkt_number_pos + i) ^= mask[i + 1];
    }
    return true;
}

bool AeadBaseCryptographer::MakeHeaderProtectMask(std::shared_ptr<IBufferReadOnly> ciphertext, BufferView sample, std::vector<uint8_t>& key,
    uint8_t* out_mask, size_t mask_cap, size_t& out_mask_length) {
    out_mask_length = 0;
    
    EVPCIPHERCTXPtr ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        LOG_ERROR("create evp cipher ctx failed");
        return false;
    }

    if (EVP_EncryptInit_ex(ctx.get(), _cipher, NULL, key.data(), sample.GetData()) != 1) {
        LOG_ERROR("EVP_EncryptInit_ex failed");
        return false;
    }

    int len = 0;
    auto read_pair = ciphertext->GetReadPair();
    if (EVP_EncryptUpdate(ctx.get(), out_mask, &len, read_pair.first, ciphertext->GetCanReadLength()) != 1) {
        LOG_ERROR("EVP_EncryptUpdate failed");
        return false;
    }
    out_mask_length = len;

    if (EVP_EncryptFinal_ex(ctx.get(), out_mask + out_mask_length, &len) != 1) {
        LOG_ERROR("EVP_EncryptFinal_ex failed");
        return false;
    }

    if (len != 0) {
        LOG_ERROR("EVP_EncryptFinal_ex out put length is not zero");
        return false;
    }

    return true;
}

void AeadBaseCryptographer::MakePacketNonce(uint8_t* nonce, std::vector<uint8_t>& iv, uint64_t pkt_number) {
    size_t i;

    memcpy(nonce, iv.data(), iv.size());
    pkt_number = PktNumberN2L(pkt_number);

    // nonce is formed by combining the packet protection IV with the packet number
    for (i = 0; i < 8; ++i) {
        nonce[iv.size() - 8 + i] ^= ((uint8_t *)&pkt_number)[i];
    }
}

uint64_t AeadBaseCryptographer::PktNumberN2L(uint64_t pkt_number) {
    return ((uint64_t)(ntohl((uint32_t)(pkt_number))) << 32 | ntohl((uint32_t)((pkt_number) >> 32)));
}

}
