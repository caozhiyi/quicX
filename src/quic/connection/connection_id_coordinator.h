#ifndef QUIC_CONNECTION_ID_COORDINATOR_H
#define QUIC_CONNECTION_ID_COORDINATOR_H

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "quic/connection/connection_id_manager.h"

namespace quicx {

// Forward declaration from common namespace
namespace common {
class IEventLoop;
}

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

    // ==================== Test-Only Methods ====================

    /**
     * @brief Test-only helper to check remote CID manager state
     * @return Shared pointer to remote CID manager
     */
    std::shared_ptr<ConnectionIDManager> GetRemoteConnectionIDManagerForTest() { return remote_conn_id_manager_; }

private:
    // Dependencies (injected)
    std::shared_ptr<::quicx::common::IEventLoop> event_loop_;
    SendManager& send_manager_;
    AddConnectionIDCallback add_conn_id_cb_;
    RetireConnectionIDCallback retire_conn_id_cb_;

    // Connection ID managers
    std::shared_ptr<ConnectionIDManager> local_conn_id_manager_;
    std::shared_ptr<ConnectionIDManager> remote_conn_id_manager_;

    // Pool size constants
    static constexpr size_t kMinLocalCIDPoolSize = 3;  // Keep at least 3 CIDs in pool
    static constexpr size_t kMaxLocalCIDPoolSize = 8;  // Generate up to 8 CIDs
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_ID_COORDINATOR_H
