#ifndef QUIC_CRYPTO_CRYPTOGRAPHER_INTERFACE
#define QUIC_CRYPTO_CRYPTOGRAPHER_INTERFACE

#include <memory>
#include <cstdint>
#include "quic/crypto/type.h"
#include "common/buffer/if_buffer.h"

// Forward declare BoringSSL cipher type in global namespace to avoid including SSL headers here
struct ssl_cipher_st;

namespace quicx {
namespace quic {

class ICryptographer {
public:
    ICryptographer();
    virtual ~ICryptographer();

    // Result codes for crypto operations
    enum class Result : int {
        kOk = 0,
        kNotInitialized,
        kInvalidArgument,
        kDeriveFailed,
        kEncryptFailed,
        kDecryptFailed,
        kHpFailed,
        kInternalError
    };

    virtual const char* GetName() = 0;

    virtual CryptographerId GetCipherId() = 0;

    virtual Result InstallSecret(const uint8_t* secret, size_t secret_len, bool is_write) = 0;

    virtual Result InstallInitSecret(const uint8_t* secret, size_t secret_len, const uint8_t *salt, size_t saltlen, bool is_server) = 0;

    virtual Result DecryptPacket(uint64_t pn, common::BufferSpan& associated_data, common::BufferSpan& ciphertext,
                             std::shared_ptr<common::IBufferWrite> out_plaintext) = 0;

    virtual Result EncryptPacket(uint64_t pn, common::BufferSpan& associated_data, common::BufferSpan& plaintext,
                             std::shared_ptr<common::IBufferWrite> out_ciphertext) = 0;

    virtual Result DecryptHeader(common::BufferSpan& ciphertext, common::BufferSpan& sample, uint8_t pn_offset,
                             uint8_t& out_packet_num_len, bool is_short) = 0;

    virtual Result EncryptHeader(common::BufferSpan& plaintext, common::BufferSpan& sample, uint8_t pn_offset,
                             size_t pkt_number_len, bool is_short) = 0;

    virtual size_t GetTagLength() = 0;

    // QUIC Key Update support: rotate secrets with new base secret
    virtual Result KeyUpdate(const uint8_t* new_base_secret, size_t secret_len, bool update_write) = 0;
    
    static CryptographerId AdapterCryptographerType(uint32_t cipher_id);
};

std::shared_ptr<ICryptographer> MakeCryptographer(const ::ssl_cipher_st *cipher);
std::shared_ptr<ICryptographer> MakeCryptographer(CryptographerId cipher);

}
}

#endif