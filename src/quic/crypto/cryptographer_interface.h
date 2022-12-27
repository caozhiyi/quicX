#ifndef QUIC_CRYPTO_CRYPTOGRAPHER_INTERFACE
#define QUIC_CRYPTO_CRYPTOGRAPHER_INTERFACE

#include <memory>
#include <cstdint>
#include "quic/crypto/type.h"
#include "common/buffer/buffer_view.h"

namespace quicx {

class IBufferReadOnly;
class CryptographerIntreface {
public:
    CryptographerIntreface();
    virtual ~CryptographerIntreface();

    virtual const char* GetName() = 0;

    virtual uint32_t GetCipherId() = 0;

    virtual bool InstallSecret(uint8_t* secret, uint32_t secret_len, bool is_write) = 0;

    virtual bool InstallInitSecret(uint8_t* secret, uint32_t secret_len, const uint8_t *salt, size_t saltlen, bool is_server) = 0;

    virtual bool DecryptPacket(uint64_t pn, BufferView associated_data, std::shared_ptr<IBufferReadOnly> ciphertext,
                             std::shared_ptr<IBufferReadOnly> out_plaintext) = 0;

    virtual bool EncryptPacket(uint64_t pn, BufferView associated_data, std::shared_ptr<IBufferReadOnly> plaintext,
                             std::shared_ptr<IBufferReadOnly> out_ciphertext) = 0;

    virtual bool DecryptHeader(std::shared_ptr<IBufferReadOnly> ciphertext, uint8_t pn_offset, bool is_short) = 0;

    virtual bool EncryptHeader(std::shared_ptr<IBufferReadOnly> plaintext, uint8_t pn_offset, bool is_short) = 0;
};

}

#endif