#ifndef QUIC_CRYPTO_AEAD_BASE_CRYPTOGRAPHER
#define QUIC_CRYPTO_AEAD_BASE_CRYPTOGRAPHER

#include <vector>
#include <openssl/ssl.h>
#include "quic/crypto/cryptographer_interface.h"

namespace quicx {
namespace quic {

class AeadBaseCryptographer:
    public ICryptographer {
public:
    AeadBaseCryptographer();
    virtual ~AeadBaseCryptographer();

    virtual bool InstallSecret(const uint8_t* secret, uint32_t secret_len, bool is_write);

    virtual bool InstallInitSecret(const uint8_t* secret, uint32_t secret_len, const uint8_t *salt, size_t saltlen, bool is_server);

    virtual bool DecryptPacket(uint64_t pkt_number, common::BufferSpan& associated_data, common::BufferSpan& ciphertext,
                             std::shared_ptr<common::IBufferWrite> out_plaintext);

    virtual bool EncryptPacket(uint64_t pkt_number, common::BufferSpan& associated_data, common::BufferSpan& plaintext,
                             std::shared_ptr<common::IBufferWrite> out_ciphertext);

    virtual bool DecryptHeader(common::BufferSpan& ciphertext, common::BufferSpan& sample, uint8_t pn_offset,
                             uint8_t& out_packet_num_len, bool is_short);

    virtual bool EncryptHeader(common::BufferSpan& plaintext, common::BufferSpan& sample, uint8_t pn_offset,
                             size_t pkt_number_len, bool is_short);

    virtual uint32_t GetTagLength() { return _aead_tag_length; }
    
protected:
    virtual bool MakeHeaderProtectMask(common::BufferSpan& sample, std::vector<uint8_t>& key,
                            uint8_t* out_mask, size_t mask_cap, size_t& out_mask_length);
    void MakePacketNonce(uint8_t* nonce, std::vector<uint8_t>& iv, uint64_t pkt_number);
    uint64_t PktNumberN2L(uint64_t pkt_number);
protected:
    struct Secret {
        std::vector<uint8_t> _key;
        std::vector<uint8_t> _iv;
        std::vector<uint8_t> _hp;
    };

    Secret _read_secret;
    Secret _write_secret;

    size_t _aead_key_length;
    size_t _aead_iv_length;
    size_t _aead_tag_length;

    size_t _cipher_key_length;
    size_t _cipher_iv_length;

    const EVP_MD *_digest;
    const EVP_AEAD *_aead;
    const EVP_CIPHER *_cipher;
};

}
}

#endif