#ifndef QUIC_CONNECTION_STATELESS_RESET_TOKEN_GENERATOR
#define QUIC_CONNECTION_STATELESS_RESET_TOKEN_GENERATOR

#include <cstdint>
#include <string>

namespace quicx {
namespace quic {

/**
 * @brief Derives RFC 9000 §10.3 Stateless Reset tokens from a connection ID.
 *
 * A Stateless Reset only works if the sender can produce the right token *after*
 * it has lost all connection state -- that is the whole point of the mechanism.
 * A randomly drawn token cannot satisfy that: there is nothing left to look it up
 * in. RFC 9000 §10.3.1 therefore recommends deriving it from the connection ID
 * with a static key, which is what this class does:
 *
 *     token = HMAC-SHA256(static_key, connection_id)[0..15]
 *
 * Properties this buys us:
 *  - Reproducible: a restarted server recomputes the token straight from the CID
 *    in the incoming packet, so peers of the pre-restart connections get reset
 *    promptly instead of hanging until idle timeout.
 *  - Unguessable: without the key an attacker cannot forge a token, and forging
 *    one is equivalent to being able to tear down arbitrary connections.
 *  - Stable across the fleet: every server sharing the key produces the same
 *    token for a given CID, so a reset works no matter which node receives the
 *    stray packet.
 *
 * Key management: the key is read from the QUICX_STATELESS_RESET_KEY environment
 * variable (hex-encoded, at least 32 bytes / 64 hex chars). Secrets are never
 * read from files or command lines here. If the variable is unset the class falls
 * back to a per-process random key, which still yields correct behaviour within a
 * single process lifetime but cannot survive a restart -- exactly the case
 * Stateless Reset exists to handle -- so it logs a warning.
 */
class StatelessResetTokenGenerator {
public:
    static constexpr uint32_t kTokenLength = 16;
    static constexpr size_t kMinKeyLength = 32;

    /** Process-wide instance; the key is loaded once, on first use. */
    static StatelessResetTokenGenerator& Instance();

    /**
     * @brief Derive the token for |cid| into |out|.
     *
     * @param cid       Connection ID bytes.
     * @param cid_len   Connection ID length (may be 0 for a zero-length CID).
     * @param out       Output buffer, must hold kTokenLength bytes.
     * @return false if |out| is null or the HMAC fails.
     */
    bool Generate(const uint8_t* cid, uint8_t cid_len, uint8_t* out) const;

    /**
     * @brief Constant-time check that |token| is the token for |cid|.
     *
     * The comparison must not short-circuit: a byte-at-a-time compare leaks how
     * many leading bytes matched, which lets an attacker recover a valid token
     * incrementally and then reset the connection at will.
     */
    bool Verify(const uint8_t* cid, uint8_t cid_len, const uint8_t* token) const;

    /** True when the key came from the environment and survives a restart. */
    bool IsKeyPersistent() const { return key_is_persistent_; }

private:
    StatelessResetTokenGenerator();

    std::string key_;
    bool key_is_persistent_{false};
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_STATELESS_RESET_TOKEN_GENERATOR
