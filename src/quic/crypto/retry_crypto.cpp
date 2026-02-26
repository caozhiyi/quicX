#include "quic/crypto/retry_crypto.h"

#include <openssl/aead.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <cstring>
#include <vector>

#include "common/log/log.h"
#include "quic/common/version.h"
#include "quic/connection/connection_id.h"
#include "quic/crypto/type.h"
#include "quic/packet/type.h"

namespace quicx {
namespace quic {

// ============================================================================
// Retry Integrity Tag Implementation
// ============================================================================

void RetryCrypto::SelectRetryKeyAndNonce(uint32_t version, const uint8_t** out_key, const uint8_t** out_nonce) {
    if (IsQuicV2(version)) {
        *out_key = kRetryIntegrityKeyV2.data();
        *out_nonce = kRetryIntegrityNonceV2.data();
    } else {
        *out_key = kRetryIntegrityKeyV1.data();
        *out_nonce = kRetryIntegrityNonceV1.data();
    }
}

bool RetryCrypto::BuildRetryPseudoPacket(const ConnectionID& original_dcid, const uint8_t* retry_packet_without_tag,
    size_t retry_packet_len, uint8_t* out_pseudo_packet, size_t& out_len) {
    // Retry Pseudo-Packet = ODCID_Len(1) || ODCID || Retry_Packet_Without_Tag
    uint8_t odcid_len = original_dcid.GetLength();
    const uint8_t* odcid_data = original_dcid.GetID();

    size_t offset = 0;

    // Write ODCID length
    out_pseudo_packet[offset++] = odcid_len;

    // Write ODCID
    std::memcpy(out_pseudo_packet + offset, odcid_data, odcid_len);
    offset += odcid_len;

    // Write Retry packet body (without tag)
    std::memcpy(out_pseudo_packet + offset, retry_packet_without_tag, retry_packet_len);
    offset += retry_packet_len;

    out_len = offset;
    return true;
}

bool RetryCrypto::ComputeRetryIntegrityTag(const ConnectionID& original_dcid, const uint8_t* retry_packet_without_tag,
    size_t retry_packet_len, uint32_t version, uint8_t* out_tag) {
    if (!retry_packet_without_tag || !out_tag) {
        common::LOG_ERROR("ComputeRetryIntegrityTag: Invalid arguments (null pointers)");
        return false;
    }

    // Select version-specific key and nonce
    const uint8_t* retry_key;
    const uint8_t* retry_nonce;
    SelectRetryKeyAndNonce(version, &retry_key, &retry_nonce);

    // Build Retry Pseudo-Packet
    std::vector<uint8_t> pseudo_packet(1 + original_dcid.GetLength() + retry_packet_len);
    size_t pseudo_len = 0;
    if (!BuildRetryPseudoPacket(original_dcid, retry_packet_without_tag, retry_packet_len, pseudo_packet.data(),
            pseudo_len)) {
        common::LOG_ERROR("ComputeRetryIntegrityTag: Failed to build pseudo-packet");
        return false;
    }

    // Initialize AES-128-GCM AEAD context
    const EVP_AEAD* aead = EVP_aead_aes_128_gcm();
    EVP_AEAD_CTX* ctx = EVP_AEAD_CTX_new(aead, retry_key, 16, kRetryIntegrityTagLength);
    if (!ctx) {
        common::LOG_ERROR("ComputeRetryIntegrityTag: Failed to create AEAD context");
        return false;
    }

    // Compute tag: seal empty plaintext with pseudo-packet as AAD
    uint8_t out_buf[kRetryIntegrityTagLength];
    size_t out_len = 0;
    int seal_ok = EVP_AEAD_CTX_seal(ctx, out_buf, &out_len, sizeof(out_buf),
        retry_nonce, 12,       // 12-byte nonce
        nullptr, 0,            // empty plaintext
        pseudo_packet.data(), pseudo_len);  // pseudo-packet as AAD

    EVP_AEAD_CTX_free(ctx);

    if (!seal_ok || out_len != kRetryIntegrityTagLength) {
        common::LOG_ERROR("ComputeRetryIntegrityTag: AEAD seal failed (seal_ok=%d, out_len=%zu)", seal_ok, out_len);
        return false;
    }

    // Copy computed tag to output
    std::memcpy(out_tag, out_buf, kRetryIntegrityTagLength);
    return true;
}

bool RetryCrypto::VerifyRetryIntegrityTag(const ConnectionID& original_dcid, const uint8_t* retry_packet_without_tag,
    size_t retry_packet_len, uint32_t version, const uint8_t* tag_to_verify) {
    if (!retry_packet_without_tag || !tag_to_verify) {
        common::LOG_ERROR("VerifyRetryIntegrityTag: Invalid arguments (null pointers)");
        return false;
    }

    // Compute expected tag
    uint8_t expected_tag[kRetryIntegrityTagLength];
    if (!ComputeRetryIntegrityTag(original_dcid, retry_packet_without_tag, retry_packet_len, version, expected_tag)) {
        common::LOG_ERROR("VerifyRetryIntegrityTag: Failed to compute expected tag");
        return false;
    }

    // Constant-time comparison (important for security)
    // Use OPENSSL_memcmp for OpenSSL, or manual implementation for compatibility
    int result = 0;
    for (size_t i = 0; i < kRetryIntegrityTagLength; i++) {
        result |= expected_tag[i] ^ tag_to_verify[i];
    }
    if (result != 0) {
        common::LOG_WARN("VerifyRetryIntegrityTag: Tag verification failed");
        return false;
    }

    return true;
}

// ============================================================================
// Retry Token HMAC Implementation
// ============================================================================

bool RetryCrypto::ComputeTokenHMAC(
    const uint8_t* data, size_t data_len, const uint8_t* key, size_t key_len, uint8_t* out_hmac, size_t& out_len) {
    if (!data || !key || !out_hmac) {
        common::LOG_ERROR("ComputeTokenHMAC: Invalid arguments (null pointers)");
        return false;
    }

    unsigned int hmac_len = 0;
    unsigned char* result =
        HMAC(EVP_sha256(), key, static_cast<int>(key_len), data, data_len, out_hmac, &hmac_len);

    if (!result || hmac_len != kTokenHMACLength) {
        common::LOG_ERROR("ComputeTokenHMAC: HMAC computation failed (result=%p, len=%u)", result, hmac_len);
        return false;
    }

    out_len = hmac_len;
    return true;
}

bool RetryCrypto::ComputeTokenHMAC(const std::string& data, const std::string& key, std::string& out_hmac) {
    uint8_t hmac_buf[kTokenHMACLength];
    size_t hmac_len = 0;

    if (!ComputeTokenHMAC(reinterpret_cast<const uint8_t*>(data.data()), data.size(),
            reinterpret_cast<const uint8_t*>(key.data()), key.size(), hmac_buf, hmac_len)) {
        return false;
    }

    out_hmac.assign(reinterpret_cast<char*>(hmac_buf), hmac_len);
    return true;
}

bool RetryCrypto::VerifyTokenHMAC(const std::string& hmac_a, const std::string& hmac_b) {
    if (hmac_a.size() != kTokenHMACLength || hmac_b.size() != kTokenHMACLength) {
        common::LOG_WARN("VerifyTokenHMAC: Invalid HMAC size (a=%zu, b=%zu)", hmac_a.size(), hmac_b.size());
        return false;
    }

    // Constant-time comparison to prevent timing attacks
    // Use manual implementation for OpenSSL compatibility
    int result = 0;
    for (size_t i = 0; i < kTokenHMACLength; i++) {
        result |= hmac_a[i] ^ hmac_b[i];
    }
    return result == 0;
}

// ============================================================================
// Random Secret Generation
// ============================================================================

bool RetryCrypto::GenerateRandomBytes(uint8_t* out_buffer, size_t length) {
    if (!out_buffer || length == 0) {
        common::LOG_ERROR("GenerateRandomBytes: Invalid arguments");
        return false;
    }

    int result = RAND_bytes(out_buffer, static_cast<int>(length));
    if (result != 1) {
        common::LOG_ERROR("GenerateRandomBytes: RAND_bytes failed");
        return false;
    }

    return true;
}

bool RetryCrypto::GenerateRandomSecret(size_t key_size, std::string& out_secret) {
    if (key_size == 0 || key_size > 1024) {
        common::LOG_ERROR("GenerateRandomSecret: Invalid key size %zu", key_size);
        return false;
    }

    std::vector<uint8_t> key_buf(key_size);
    if (!GenerateRandomBytes(key_buf.data(), key_size)) {
        return false;
    }

    out_secret.assign(reinterpret_cast<char*>(key_buf.data()), key_size);
    return true;
}

}  // namespace quic
}  // namespace quicx
