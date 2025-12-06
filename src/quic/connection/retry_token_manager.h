#ifndef QUIC_CONNECTION_RETRY_TOKEN_MANAGER
#define QUIC_CONNECTION_RETRY_TOKEN_MANAGER

#include <mutex>
#include <string>
#include "common/network/address.h"
#include "quic/connection/connection_id.h"

namespace quicx {
namespace quic {

/**
 * @brief Manages Retry token generation and validation for QUIC connections.
 *
 * Implements RFC 9000 Retry mechanism using HMAC-SHA256 for token generation.
 * Tokens include timestamp for expiration checking and are bound to client
 * address and original destination connection ID.
 *
 * Thread-safe for use across multiple worker threads.
 */
class RetryTokenManager {
public:
    RetryTokenManager();
    ~RetryTokenManager() = default;

    /**
     * @brief Generate a Retry token for a client.
     *
     * Token format: timestamp (8 bytes) || HMAC-SHA256 (32 bytes)
     * HMAC is computed over: client_ip || timestamp || original_dcid
     *
     * @param client_addr Client's network address
     * @param original_dcid Original destination connection ID from Initial packet
     * @return Base64-encoded token string
     */
    std::string GenerateToken(const common::Address& client_addr, const ConnectionID& original_dcid);

    /**
     * @brief Validate a Retry token from a client.
     *
     * Checks:
     * 1. Token format and size
     * 2. Token age (not expired)
     * 3. HMAC signature (matches current or previous secret)
     *
     * @param token Token string from client
     * @param client_addr Client's network address
     * @param original_dcid Original destination connection ID
     * @param max_age_seconds Maximum token age in seconds (default: 60)
     * @return true if token is valid, false otherwise
     */
    bool ValidateToken(const std::string& token, const common::Address& client_addr, const ConnectionID& original_dcid,
        uint64_t max_age_seconds = 60);

    /**
     * @brief Rotate the secret key.
     *
     * Previous secret is kept for one rotation period to allow validation
     * of tokens issued just before rotation.
     */
    void RotateSecret();

private:
    /**
     * @brief Compute HMAC-SHA256 over data using current secret.
     */
    std::string ComputeHMAC(const std::string& data);

    /**
     * @brief Generate a new random secret key.
     */
    void GenerateRandomSecret();

    std::string current_secret_;
    std::string previous_secret_;  // For validation during rotation window
    std::mutex mutex_;
    uint64_t last_rotation_time_;

    static constexpr size_t SECRET_SIZE = 32;
    static constexpr uint64_t ROTATION_INTERVAL = 86400;  // 24 hours
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_RETRY_TOKEN_MANAGER
