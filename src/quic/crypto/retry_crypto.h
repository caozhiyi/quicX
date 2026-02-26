#ifndef QUIC_CRYPTO_RETRY_CRYPTO
#define QUIC_CRYPTO_RETRY_CRYPTO

#include <cstddef>
#include <cstdint>
#include <string>

namespace quicx {
namespace quic {

// Forward declarations
class ConnectionID;

/**
 * @brief Retry mechanism cryptography utilities
 * 
 * Provides all cryptographic operations for QUIC Retry mechanism (RFC 9000 § 8.1):
 * 1. Retry Integrity Tag computation (RFC 9001 § 5.8, RFC 9369 for v2)
 * 2. Retry Token generation and validation (HMAC-based)
 * 
 * This module encapsulates all OpenSSL operations to separate crypto logic from
 * business logic in ServerWorker and RetryTokenManager.
 */
class RetryCrypto {
public:
    // ========================================================================
    // Retry Integrity Tag (RFC 9001 § 5.8)
    // ========================================================================

    /**
     * @brief Compute Retry Integrity Tag using AES-128-GCM
     * 
     * The Retry Integrity Tag is computed over the Retry Pseudo-Packet:
     *   Retry Pseudo-Packet = ODCID_Len(1) || ODCID || Retry_Packet_Without_Tag
     * 
     * @param original_dcid Original Destination Connection ID from client's Initial
     * @param retry_packet_without_tag Encoded Retry packet body (excluding the 16-byte tag)
     * @param retry_packet_len Length of retry_packet_without_tag
     * @param version QUIC version (for selecting key/nonce: v1 or v2)
     * @param out_tag Output buffer for 16-byte integrity tag (must be at least 16 bytes)
     * @return true if tag computed successfully, false on error
     */
    static bool ComputeRetryIntegrityTag(const ConnectionID& original_dcid, const uint8_t* retry_packet_without_tag,
        size_t retry_packet_len, uint32_t version, uint8_t* out_tag);

    /**
     * @brief Verify Retry Integrity Tag
     * 
     * @param original_dcid Original Destination Connection ID
     * @param retry_packet_without_tag Encoded Retry packet body (excluding tag)
     * @param retry_packet_len Length of retry_packet_without_tag
     * @param version QUIC version
     * @param tag_to_verify 16-byte tag from received Retry packet
     * @return true if tag is valid, false otherwise
     */
    static bool VerifyRetryIntegrityTag(const ConnectionID& original_dcid, const uint8_t* retry_packet_without_tag,
        size_t retry_packet_len, uint32_t version, const uint8_t* tag_to_verify);

    // ========================================================================
    // Retry Token HMAC (RFC 9000 § 8.1.3)
    // ========================================================================

    /**
     * @brief Compute HMAC-SHA256 for Retry Token
     * 
     * @param data Input data to compute HMAC over
     * @param data_len Length of input data
     * @param key HMAC secret key
     * @param key_len Length of HMAC key
     * @param out_hmac Output buffer for HMAC (must be at least 32 bytes)
     * @param out_len Output length of HMAC (will be 32)
     * @return true on success, false on error
     */
    static bool ComputeTokenHMAC(const uint8_t* data, size_t data_len, const uint8_t* key, size_t key_len,
        uint8_t* out_hmac, size_t& out_len);

    /**
     * @brief Convenience overload for std::string
     * 
     * @param data Input data
     * @param key HMAC key
     * @param out_hmac Output HMAC string (32 bytes)
     * @return true on success, false on error
     */
    static bool ComputeTokenHMAC(const std::string& data, const std::string& key, std::string& out_hmac);

    /**
     * @brief Constant-time HMAC comparison (for token verification)
     * 
     * @param hmac_a First HMAC
     * @param hmac_b Second HMAC
     * @return true if HMACs are equal, false otherwise
     */
    static bool VerifyTokenHMAC(const std::string& hmac_a, const std::string& hmac_b);

    // ========================================================================
    // Random Secret Generation
    // ========================================================================

    /**
     * @brief Generate cryptographically secure random bytes
     * 
     * @param out_buffer Output buffer for random bytes
     * @param length Number of random bytes to generate
     * @return true on success, false on error
     */
    static bool GenerateRandomBytes(uint8_t* out_buffer, size_t length);

    /**
     * @brief Generate a random secret key for token HMAC
     * 
     * @param key_size Size of secret key in bytes (typically 32 for SHA256)
     * @param out_secret Output secret key
     * @return true on success, false on error
     */
    static bool GenerateRandomSecret(size_t key_size, std::string& out_secret);

    // Constants
    static constexpr size_t kRetryIntegrityTagLength = 16;  // AES-128-GCM tag
    static constexpr size_t kTokenHMACLength = 32;          // HMAC-SHA256 output
    static constexpr size_t kRecommendedSecretSize = 32;    // For HMAC-SHA256

private:
    /**
     * @brief Build Retry Pseudo-Packet for AEAD AAD
     */
    static bool BuildRetryPseudoPacket(const ConnectionID& original_dcid, const uint8_t* retry_packet_without_tag,
        size_t retry_packet_len, uint8_t* out_pseudo_packet, size_t& out_len);

    /**
     * @brief Select version-specific Retry Integrity key and nonce
     */
    static void SelectRetryKeyAndNonce(uint32_t version, const uint8_t** out_key, const uint8_t** out_nonce);
};

}  // namespace quic
}  // namespace quicx

#endif
