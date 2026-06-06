#ifndef QUIC_CONNECTION_RETRY_TOKEN_MANAGER
#define QUIC_CONNECTION_RETRY_TOKEN_MANAGER

#include <chrono>
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
 * address (IP + port) and original destination connection ID.
 *
 * Wire format (big-endian, fixed across architectures):
 *   timestamp_be (8 bytes, ms since unix epoch)
 *     || cid_len (1 byte)
 *     || original_dcid (cid_len bytes)
 *     || HMAC-SHA256 (32 bytes)
 *
 * HMAC input (binds token to client identity, big-endian):
 *   client_ip (string)
 *     || client_port_be (2 bytes)
 *     || timestamp_be (8 bytes)
 *     || cid_len (1 byte)
 *     || original_dcid (cid_len bytes)
 *
 * Note on rotation timing: secret rotation uses a steady_clock so it is
 * unaffected by wall-clock jumps. Token timestamps remain wall-clock
 * (system_clock) so token age tracks real elapsed time across restarts.
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
     * @param client_addr Client's network address (both IP and port are bound)
     * @param original_dcid Original destination connection ID from Initial packet
     * @return Token string (see class doc for wire format)
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
     * @param[out] out_original_dcid Output parameter to store the extracted Original DCID
     * @param max_age_seconds Maximum token age in seconds (default: 60)
     * @return true if token is valid, false otherwise
     */
    bool ValidateToken(const std::string& token, const common::Address& client_addr, ConnectionID& out_original_dcid,
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

    /**
     * @brief Build the HMAC payload (client_ip || port_be || timestamp_be || cid_len || cid).
     *
     * Centralized so GenerateToken() and ValidateToken() can never drift.
     * All multi-byte fields are written big-endian to be stable across
     * heterogeneous-architecture clusters.
     */
    static std::string BuildHmacPayload(const common::Address& client_addr, uint64_t timestamp_ms,
        uint8_t cid_len, const uint8_t* cid_bytes);

    std::string current_secret_;
    std::string previous_secret_;  // For validation during rotation window
    std::mutex mutex_;

    // Monotonic clock for rotation cadence: not affected by wall-clock jumps
    // (NTP step, manual settime, suspend/resume, etc). Initialized at ctor.
    std::chrono::steady_clock::time_point last_rotation_time_;

    static constexpr size_t SECRET_SIZE = 32;
    static constexpr std::chrono::seconds kRotationInterval{86400};  // 24 hours
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_RETRY_TOKEN_MANAGER
