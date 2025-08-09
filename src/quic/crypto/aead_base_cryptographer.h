#ifndef QUIC_CRYPTO_AEAD_BASE_CRYPTOGRAPHER
#define QUIC_CRYPTO_AEAD_BASE_CRYPTOGRAPHER

#include <vector>
// Opaque to headers; all OpenSSL/BoringSSL includes are in .cpp
#include "quic/crypto/if_cryptographer.h"

namespace quicx {
namespace quic {

class AeadBaseCryptographer:
    public ICryptographer {
public:
    AeadBaseCryptographer();
    virtual ~AeadBaseCryptographer();

    virtual Result InstallSecret(const uint8_t* secret, size_t secret_len, bool is_write) override;

    virtual Result InstallInitSecret(const uint8_t* secret, size_t secret_len, const uint8_t *salt, size_t saltlen, bool is_server) override;

    virtual Result DecryptPacket(uint64_t pkt_number, common::BufferSpan& associated_data, common::BufferSpan& ciphertext,
                             std::shared_ptr<common::IBufferWrite> out_plaintext) override;

    virtual Result EncryptPacket(uint64_t pkt_number, common::BufferSpan& associated_data, common::BufferSpan& plaintext,
                             std::shared_ptr<common::IBufferWrite> out_ciphertext) override;

    virtual Result DecryptHeader(common::BufferSpan& ciphertext, common::BufferSpan& sample, uint8_t pn_offset,
                             uint8_t& out_packet_num_len, bool is_short) override;

    virtual Result EncryptHeader(common::BufferSpan& plaintext, common::BufferSpan& sample, uint8_t pn_offset,
                             size_t pkt_number_len, bool is_short) override;

    virtual size_t GetTagLength() override { return aead_tag_length_; }

    // Rotate secrets for Key Update (RFC 9001 §6)
    virtual Result KeyUpdate(const uint8_t* new_base_secret, size_t secret_len, bool update_write) override;

    // Optional rotation thresholds (not enforced internally, only stored/exposed)
    void SetKeyUpdateThresholdByPn(uint64_t pn_threshold) { pn_rotate_threshold_ = pn_threshold; }
    void SetKeyUpdateThresholdByTimeMs(uint64_t ms) { time_rotate_threshold_ms_ = ms; }
    uint64_t GetKeyUpdateThresholdByPn() const { return pn_rotate_threshold_; }
    uint64_t GetKeyUpdateThresholdByTimeMs() const { return time_rotate_threshold_ms_; }
    bool WasKeyUpdated() const { return key_updated_flag_; }
    
protected:
    // Derive header protection mask into out_mask. Chooses cipher context based on key.
    virtual bool MakeHeaderProtectMask(common::BufferSpan& sample, std::vector<uint8_t>& key,
                            uint8_t* out_mask, size_t mask_cap, size_t& out_mask_length);
    void MakePacketNonce(uint8_t* nonce, std::vector<uint8_t>& iv, uint64_t pkt_number);
protected:
    struct Secret {
        std::vector<uint8_t> key_;
        std::vector<uint8_t> iv_;
        std::vector<uint8_t> hp_;
    };
    void CleanSecret(Secret& s);

    Secret read_secret_;
    Secret write_secret_;

    size_t aead_key_length_;
    size_t aead_iv_length_;
    size_t aead_tag_length_;

    size_t cipher_key_length_;
    size_t cipher_iv_length_;

    const EVP_MD *digest_;
    const EVP_AEAD *aead_;
    const EVP_CIPHER *cipher_;

    // Cached contexts for performance
    EVPAEADCTXPtr read_aead_ctx_;
    EVPAEADCTXPtr write_aead_ctx_;
    EVPCIPHERCTXPtr hp_read_ctx_;
    EVPCIPHERCTXPtr hp_write_ctx_;

    // Key update bookkeeping
    uint64_t pn_rotate_threshold_ = 0;       // user-configurable; not enforced
    uint64_t time_rotate_threshold_ms_ = 0;  // user-configurable; not enforced
    bool key_updated_flag_ = false;
};

}
}

#endif