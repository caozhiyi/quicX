#ifndef QUIC_CRYPTO_CRYPTOGRAPHER_INTERFACE
#define QUIC_CRYPTO_CRYPTOGRAPHER_INTERFACE

#include <cstdint>
#include <memory>
#include "common/buffer/if_buffer.h"
#include "quic/crypto/type.h"

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

    // Version-aware secret installation (uses version-specific labels)
    virtual Result InstallSecretWithVersion(
        const uint8_t* secret, size_t secret_len, bool is_write, uint32_t version) = 0;

    virtual Result InstallInitSecret(
        const uint8_t* secret, size_t secret_len, const uint8_t* salt, size_t saltlen, bool is_server) = 0;

    // Version-aware Initial secret installation (RFC 9369 support)
    virtual Result InstallInitSecretWithVersion(
        const uint8_t* secret, size_t secret_len, uint32_t version, bool is_server) = 0;

    virtual Result DecryptPacket(uint64_t pn, common::BufferSpan& associated_data, common::BufferSpan& ciphertext,
        std::shared_ptr<common::IBuffer> out_plaintext) = 0;

    // RFC 9001 §6: Decrypt packet using the previous read key (for reordered packets after Key Update)
    virtual Result DecryptPacketWithPrevKey(uint64_t pn, common::BufferSpan& associated_data,
        common::BufferSpan& ciphertext, std::shared_ptr<common::IBuffer> out_plaintext) = 0;

    virtual Result EncryptPacket(uint64_t pn, common::BufferSpan& associated_data, common::BufferSpan& plaintext,
        std::shared_ptr<common::IBuffer> out_ciphertext) = 0;

    // `pn_offset` is the distance from the start of the packet to the packet
    // number field. It must be at least 32 bits wide: a long header carries two
    // connection IDs of up to 20 B each and an Initial sent in response to a
    // Retry also echoes the server's token, which is commonly 256 B (aioquic),
    // so the offset routinely exceeds 255. Narrowing it to uint8_t silently
    // wraps the offset modulo 256 and applies the header-protection mask deep
    // inside the token instead of the packet number, corrupting the token and
    // leaving the packet number unprotected.
    virtual Result DecryptHeader(common::BufferSpan& ciphertext, common::BufferSpan& sample, uint32_t pn_offset,
        uint8_t& out_packet_num_len, bool is_short) = 0;

    // RFC 9001 §6: Check if previous read key is available (for Key Update fallback)
    virtual bool HasPrevReadKey() const = 0;

    // See DecryptHeader above for why `pn_offset` must not be narrowed.
    virtual Result EncryptHeader(common::BufferSpan& plaintext, common::BufferSpan& sample, uint32_t pn_offset,
        size_t pkt_number_len, bool is_short) = 0;

    virtual size_t GetTagLength() = 0;

    // QUIC Key Update support: rotate secrets with new base secret
    virtual Result KeyUpdate(const uint8_t* new_base_secret, size_t secret_len, bool update_write) = 0;

    // Version-aware Key Update (RFC 9369)
    virtual Result KeyUpdateWithVersion(
        const uint8_t* new_base_secret, size_t secret_len, bool update_write, uint32_t version) = 0;

    // Set/Get current QUIC version for this cryptographer
    virtual void SetVersion(uint32_t version) = 0;
    virtual uint32_t GetVersion() const = 0;

    static CryptographerId AdapterCryptographerType(uint32_t cipher_id);
};

std::shared_ptr<ICryptographer> MakeCryptographer(const ::ssl_cipher_st* cipher);
std::shared_ptr<ICryptographer> MakeCryptographer(CryptographerId cipher);

}  // namespace quic
}  // namespace quicx

#endif