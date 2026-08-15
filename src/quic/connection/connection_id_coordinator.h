#ifndef QUIC_CONNECTION_ID_COORDINATOR_H
#define QUIC_CONNECTION_ID_COORDINATOR_H

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "quic/connection/connection_id_manager.h"

namespace quicx {

// Forward declaration from common namespace
namespace common {
class IEventLoop;
class QlogTrace;
}  // namespace common

namespace quic {

// Forward declarations
class SendManager;

/**
 * @brief Connection ID coordinator
 *
 * Responsibilities:
 * - Coordinate local and remote connection ID management
 * - Handle connection ID rotation
 * - Manage connection ID pool
 * - Send NEW_CONNECTION_ID and RETIRE_CONNECTION_ID frames
 */
class ConnectionIDCoordinator {
public:
    using AddConnectionIDCallback = std::function<void(ConnectionID&)>;
    using RetireConnectionIDCallback = std::function<void(ConnectionID&)>;

    ConnectionIDCoordinator(std::shared_ptr<::quicx::common::IEventLoop> event_loop, SendManager& send_manager,
        AddConnectionIDCallback add_cb, RetireConnectionIDCallback retire_cb);

    ~ConnectionIDCoordinator() = default;

    // ==================== Peer Stateless Reset Tokens ====================

    /**
     * @brief Remember a stateless reset token the peer associated with one of its
     *        connection IDs (from its transport parameters or a NEW_CONNECTION_ID
     *        frame).
     *
     * RFC 9000 §10.3.1: a Stateless Reset is indistinguishable from a normal
     * packet except that its last 16 bytes equal one of these tokens. Recognising
     * one is the only way to learn that the peer has lost our connection state;
     * without it we retransmit until the idle timeout.
     *
     * @param token Pointer to kStatelessResetTokenLength bytes.
     */
    void AddPeerStatelessResetToken(const uint8_t* token);

    /**
     * @brief Constant-time test of whether |token| is one of the peer's tokens.
     *
     * Constant time per §10.3: a timing-variable compare would let an observer
     * probe for valid tokens, and a valid token is enough to kill the connection.
     *
     * @param token Pointer to kStatelessResetTokenLength bytes.
     */
    bool IsPeerStatelessResetToken(const uint8_t* token) const;

    // ==================== Initialization ====================

    /**
     * @brief Initialize local and remote connection ID managers
     */
    void Initialize();

    // ==================== Connection ID Operations ====================

    /**
     * @brief Add connection ID (callback to upper layer)
     * @param id Connection ID to add
     */
    void AddConnectionId(ConnectionID& id);

    /**
     * @brief Retire connection ID (callback to upper layer)
     * @param id Connection ID to retire
     */
    void RetireConnectionId(ConnectionID& id);

    /**
     * @brief Get current connection ID hash (for routing)
     * @return Connection ID hash
     */
    uint64_t GetConnectionIDHash() const;

    /**
     * @brief Get all local CID hashes (for cleanup on close)
     * @return Vector of all local CID hashes
     */
    std::vector<uint64_t> GetAllLocalCIDHashes() const;

    // ==================== Connection ID Pool Management ====================

    /**
     * @brief Check and replenish local CID pool
     * Generates new CIDs and sends NEW_CONNECTION_ID frames when pool is low
     */
    void CheckAndReplenishLocalCIDPool();

    /**
     * @brief Rotate remote connection ID after migration
     * Switches to next remote CID and sends RETIRE_CONNECTION_ID for old one
     * @return true if rotation succeeded, false otherwise
     */
    bool RotateRemoteConnectionID();

    /**
     * @brief Flush a previously deferred RETIRE_CONNECTION_ID for the remote CID
     * rotated away from during migration.
     *
     * RotateRemoteConnectionID() intentionally does NOT send the RETIRE frame
     * immediately: retiring a CID while the path that uses it is still active
     * makes the peer delete that path and abort the connection (e.g. picoquic
     * "Cannot delete path through which packet arrives"). This is called from
     * PathManager::OnPathResponse once the new migration path has been
     * validated, at which point retiring the old CID is safe.
     */
    void RetirePendingRemoteConnectionID();

    /**
     * @brief Set peer's active connection ID limit (from transport parameters)
     * @param limit Active connection ID limit
     */
    void SetPeerActiveConnectionIDLimit(uint64_t limit);

    // ==================== Accessors ====================

    /**
     * @brief Get local connection ID manager
     * @return Shared pointer to local CID manager
     */
    std::shared_ptr<ConnectionIDManager> GetLocalConnectionIDManager() { return local_conn_id_manager_; }

    /**
     * @brief Get remote connection ID manager
     * @return Shared pointer to remote CID manager
     */
    std::shared_ptr<ConnectionIDManager> GetRemoteConnectionIDManager() { return remote_conn_id_manager_; }

    /**
     * @brief Get remote connection ID manager (const)
     * @return Shared pointer to remote CID manager
     */
    std::shared_ptr<const ConnectionIDManager> GetRemoteConnectionIDManager() const { return remote_conn_id_manager_; }

    /**
     * @brief Set qlog trace for connection ID events
     */
    void SetQlogTrace(std::shared_ptr<common::QlogTrace> trace) { qlog_trace_ = trace; }

    // ==================== Test-Only Methods ====================

    /**
     * @brief Test-only helper to check remote CID manager state
     * @return Shared pointer to remote CID manager
     */
    std::shared_ptr<ConnectionIDManager> GetRemoteConnectionIDManagerForTest() { return remote_conn_id_manager_; }

private:
    // Dependencies (injected)
    std::weak_ptr<::quicx::common::IEventLoop> event_loop_;
    SendManager& send_manager_;
    AddConnectionIDCallback add_conn_id_cb_;
    RetireConnectionIDCallback retire_conn_id_cb_;

    // Connection ID managers
    std::shared_ptr<ConnectionIDManager> local_conn_id_manager_;
    std::shared_ptr<ConnectionIDManager> remote_conn_id_manager_;

    // Pool size constants
    static constexpr size_t kMinLocalCIDPoolSize = 3;  // Keep at least 3 CIDs in pool
    static constexpr size_t kMaxLocalCIDPoolSize = 8;  // Generate up to 8 CIDs

    uint64_t peer_active_cid_limit_{2};  // Default to 2 (RFC 9000)

    // Qlog trace for connection ID events
    std::shared_ptr<common::QlogTrace> qlog_trace_;

    // Deferred RETIRE_CONNECTION_ID state. When the remote CID is rotated during
    // migration, we remember the sequence number of the CID we rotated away from
    // and only emit the RETIRE_CONNECTION_ID once the new path is validated
    // (RetirePendingRemoteConnectionID), per RFC 9000 §9.2 / peer path-deletion
    // requirements.
    uint64_t pending_retire_remote_seq_{0};
    bool has_pending_retire_remote_cid_{false};

    // Stateless reset tokens advertised by the peer. Bounded so a peer that floods
    // NEW_CONNECTION_ID frames cannot grow this without limit; the peer's own
    // active_connection_id_limit already bounds how many CIDs are useful.
    static constexpr size_t kMaxPeerResetTokens = 32;
    std::vector<std::array<uint8_t, 16>> peer_reset_tokens_;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_ID_COORDINATOR_H
