#ifndef QUIC_CRYPTO_TYPE
#define QUIC_CRYPTO_TYPE

#include <openssl/evp.h>
#include <array>
#include <cstdint>

#include "common/util/c_smart_ptr.h"
#include "quic/common/version.h"

namespace quicx {
namespace quic {

// ============================================================================
// QUIC v1 (RFC 9001) - HKDF Labels and Salt
// ============================================================================

// HKDF expand labels for QUIC v1
inline constexpr std::array<uint8_t, 15> kTlsLabelClient = {
    't', 'l', 's', '1', '3', ' ', 'c', 'l', 'i', 'e', 'n', 't', ' ', 'i', 'n'};
inline constexpr std::array<uint8_t, 15> kTlsLabelServer = {
    't', 'l', 's', '1', '3', ' ', 's', 'e', 'r', 'v', 'e', 'r', ' ', 'i', 'n'};

// QUIC v1 packet protection labels (RFC 9001)
inline constexpr std::array<uint8_t, 14> kTlsLabelKeyV1 = {
    't', 'l', 's', '1', '3', ' ', 'q', 'u', 'i', 'c', ' ', 'k', 'e', 'y'};
inline constexpr std::array<uint8_t, 13> kTlsLabelHpV1 = {
    't', 'l', 's', '1', '3', ' ', 'q', 'u', 'i', 'c', ' ', 'h', 'p'};
inline constexpr std::array<uint8_t, 13> kTlsLabelIvV1 = {
    't', 'l', 's', '1', '3', ' ', 'q', 'u', 'i', 'c', ' ', 'i', 'v'};
inline constexpr std::array<uint8_t, 13> kTlsLabelKuV1 = {
    't', 'l', 's', '1', '3', ' ', 'q', 'u', 'i', 'c', ' ', 'k', 'u'};

// Aliases for backward compatibility (default to v1)
inline constexpr auto& kTlsLabelKey = kTlsLabelKeyV1;
inline constexpr auto& kTlsLabelHp = kTlsLabelHpV1;
inline constexpr auto& kTlsLabelIv = kTlsLabelIvV1;
inline constexpr auto& kTlsLabelKu = kTlsLabelKuV1;

// QUIC v1 Initial Salt (RFC 9001)
inline constexpr std::array<uint8_t, 20> kInitialSaltV1 = {
    0x38, 0x76, 0x2c, 0xf7, 0xf5, 0x59, 0x34, 0xb3, 0x4d, 0x17,
    0x9a, 0xe6, 0xa4, 0xc8, 0x0c, 0xad, 0xcc, 0xbb, 0x7f, 0x0a};

// ============================================================================
// QUIC v2 (RFC 9369) - HKDF Labels and Salt
// ============================================================================

// QUIC v2 packet protection labels (RFC 9369)
// Note: "quicv2" instead of "quic" in the label
inline constexpr std::array<uint8_t, 16> kTlsLabelKeyV2 = {
    't', 'l', 's', '1', '3', ' ', 'q', 'u', 'i', 'c', 'v', '2', ' ', 'k', 'e', 'y'};
inline constexpr std::array<uint8_t, 15> kTlsLabelHpV2 = {
    't', 'l', 's', '1', '3', ' ', 'q', 'u', 'i', 'c', 'v', '2', ' ', 'h', 'p'};
inline constexpr std::array<uint8_t, 15> kTlsLabelIvV2 = {
    't', 'l', 's', '1', '3', ' ', 'q', 'u', 'i', 'c', 'v', '2', ' ', 'i', 'v'};
inline constexpr std::array<uint8_t, 15> kTlsLabelKuV2 = {
    't', 'l', 's', '1', '3', ' ', 'q', 'u', 'i', 'c', 'v', '2', ' ', 'k', 'u'};

// QUIC v2 Initial Salt (RFC 9369)
inline constexpr std::array<uint8_t, 20> kInitialSaltV2 = {
    0x0d, 0xed, 0xe3, 0xde, 0xf7, 0x00, 0xa6, 0xdb, 0x81, 0x93,
    0x81, 0xbe, 0x6e, 0x26, 0x9d, 0xcb, 0xf9, 0xbd, 0x2e, 0xd9};

// ============================================================================
// QUIC v2 Retry Integrity (RFC 9369)
// ============================================================================

// QUIC v1 Retry Integrity Key (RFC 9001)
inline constexpr std::array<uint8_t, 16> kRetryIntegrityKeyV1 = {
    0xbe, 0x0c, 0x69, 0x0b, 0x9f, 0x66, 0x57, 0x5a,
    0x1d, 0x76, 0x6b, 0x54, 0xe3, 0x68, 0xc8, 0x4e};
inline constexpr std::array<uint8_t, 12> kRetryIntegrityNonceV1 = {
    0x46, 0x15, 0x99, 0xd3, 0x5d, 0x63, 0x2b, 0xf2, 0x23, 0x98, 0x25, 0xbb};

// QUIC v2 Retry Integrity Key (RFC 9369)
inline constexpr std::array<uint8_t, 16> kRetryIntegrityKeyV2 = {
    0x8f, 0xb4, 0xb0, 0x1b, 0x56, 0xac, 0x48, 0xe2,
    0x60, 0xfb, 0xcb, 0xce, 0xad, 0x7c, 0xcc, 0x92};
inline constexpr std::array<uint8_t, 12> kRetryIntegrityNonceV2 = {
    0xd8, 0x69, 0x69, 0xbc, 0x2d, 0x7c, 0x6d, 0x99, 0x90, 0xef, 0xb0, 0x4a};

// ============================================================================
// Backward Compatibility Aliases
// ============================================================================

// Default to v1 for backward compatibility
inline constexpr auto& kInitialSalt = kInitialSaltV1;
inline constexpr auto& kRetryIntegrityKey = kRetryIntegrityKeyV1;
inline constexpr auto& kRetryIntegrityNonce = kRetryIntegrityNonceV1;

// ============================================================================
// Common Constants
// ============================================================================

inline constexpr std::array<uint8_t, 5> kHeaderMask = {0, 0, 0, 0, 0};

inline constexpr size_t kMaxInitSecretLength = 32;
inline constexpr size_t kHeaderProtectSampleLength = 16;
inline constexpr size_t kHeaderProtectMaskLength = 5;
inline constexpr size_t kPacketNonceLength = 12;  // RFC 9001 §5.3: QUIC uses 12-byte nonces
inline constexpr size_t kCryptoLevelCount = 4;

enum CryptographerId : uint16_t {
    kCipherIdUnknown = 0,
    kCipherIdAes128GcmSha256,
    kCipherIdAes256GcmSha384,
    kCipherIdChaCha20Poly1305Sha256,
};

using EVPCIPHERCTXPtr = common::CSmartPtr<EVP_CIPHER_CTX, EVP_CIPHER_CTX_free>;
using EVPAEADCTXPtr = common::CSmartPtr<EVP_AEAD_CTX, EVP_AEAD_CTX_free>;

// ============================================================================
// Version-aware Helper Functions
// ============================================================================

// Get Initial Salt based on QUIC version
inline const uint8_t* GetInitialSalt(uint32_t version) {
    // QUIC v2
    if (version == kQuicVersion2) {
        return kInitialSaltV2.data();
    }
    // Default to v1 salt
    return kInitialSaltV1.data();
}

inline size_t GetInitialSaltLength(uint32_t version) {
    return 20;  // Both v1 and v2 use 20-byte salt
}

// Get Retry Integrity Key based on QUIC version
inline const uint8_t* GetRetryIntegrityKey(uint32_t version) {
    if (version == kQuicVersion2) {
        return kRetryIntegrityKeyV2.data();
    }
    return kRetryIntegrityKeyV1.data();
}

inline const uint8_t* GetRetryIntegrityNonce(uint32_t version) {
    if (version == kQuicVersion2) {
        return kRetryIntegrityNonceV2.data();
    }
    return kRetryIntegrityNonceV1.data();
}

// Labels structure for version-aware key derivation
struct QuicLabels {
    const uint8_t* key;
    size_t key_len;
    const uint8_t* hp;
    size_t hp_len;
    const uint8_t* iv;
    size_t iv_len;
    const uint8_t* ku;
    size_t ku_len;
};

// Get HKDF labels based on QUIC version
inline QuicLabels GetQuicLabels(uint32_t version) {
    if (version == kQuicVersion2) {
        // QUIC v2
        return QuicLabels{
            kTlsLabelKeyV2.data(), kTlsLabelKeyV2.size(),
            kTlsLabelHpV2.data(), kTlsLabelHpV2.size(),
            kTlsLabelIvV2.data(), kTlsLabelIvV2.size(),
            kTlsLabelKuV2.data(), kTlsLabelKuV2.size()
        };
    }
    // Default to v1
    return QuicLabels{
        kTlsLabelKeyV1.data(), kTlsLabelKeyV1.size(),
        kTlsLabelHpV1.data(), kTlsLabelHpV1.size(),
        kTlsLabelIvV1.data(), kTlsLabelIvV1.size(),
        kTlsLabelKuV1.data(), kTlsLabelKuV1.size()
    };
}

}  // namespace quic
}  // namespace quicx

#endif