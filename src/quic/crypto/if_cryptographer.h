#ifndef QUIC_CRYPTO_CRYPTOGRAPHER_INTERFACE
#define QUIC_CRYPTO_CRYPTOGRAPHER_INTERFACE

#include <memory>
#include <cstdint>
#include "quic/crypto/type.h"
#include "common/buffer/if_buffer.h"

namespace quicx {
namespace quic {

class ICryptographer {
public:
    ICryptographer();
    virtual ~ICryptographer();

    virtual const char* GetName() = 0;

    virtual CryptographerId GetCipherId() = 0;

    virtual bool InstallSecret(const uint8_t* secret, uint32_t secret_len, bool is_write) = 0;

    virtual bool InstallInitSecret(const uint8_t* secret, uint32_t secret_len, const uint8_t *salt, size_t saltlen, bool is_server) = 0;

    virtual bool DecryptPacket(uint64_t pn, common::BufferSpan& associated_data, common::BufferSpan& ciphertext,
                             std::shared_ptr<common::IBufferWrite> out_plaintext) = 0;

    virtual bool EncryptPacket(uint64_t pn, common::BufferSpan& associated_data, common::BufferSpan& plaintext,
                             std::shared_ptr<common::IBufferWrite> out_ciphertext) = 0;

    virtual bool DecryptHeader(common::BufferSpan& ciphertext, common::BufferSpan& sample, uint8_t pn_offset,
                             uint8_t& out_packet_num_len, bool is_short) = 0;

    virtual bool EncryptHeader(common::BufferSpan& plaintext, common::BufferSpan& sample, uint8_t pn_offset,
                             size_t pkt_number_len, bool is_short) = 0;

    virtual uint32_t GetTagLength() = 0;
    
    static CryptographerId AdapterCryptographerType(uint32_t cipher_id);
};

std::shared_ptr<ICryptographer> MakeCryptographer(const SSL_CIPHER *cipher);
std::shared_ptr<ICryptographer> MakeCryptographer(CryptographerId cipher);

}
}

#endif