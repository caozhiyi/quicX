#ifndef QUIC_CONNECTION_PATH_MANAGER_H
#define QUIC_CONNECTION_PATH_MANAGER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "common/network/address.h"
#include "common/timer/timer_task.h"
#include "quic/include/type.h"

namespace quicx {

// Forward declarations from common namespace
namespace common {
class IEventLoop;
}

namespace quic {

// Forward declarations
class SendManager;
class ConnectionIDCoordinator;
class TransportParam;
class IFrame;

/**
 * @brief Path manager for connection migration and path validation
 *
 * Responsibilities:
 * - Path validation (PATH_CHALLENGE/PATH_RESPONSE)
 * - Path migration support (client-initiated and NAT rebinding)
 * - Anti-amplification protection during path validation
 * - Candidate address queue management
 * - Local address change support for production-grade migration
 *
 * RFC 9000 Section 9: Connection Migration
 */
class PathManager {
public:
    using ToSendFrameCallback = std::function<void(std::shared_ptr<IFrame>)>;
    using ActiveSendCallback = std::function<void()>;
    using SetPeerAddressCallback = std::function<void(const ::quicx::common::Address&)>;
    using MigrationCompleteCallback = std::function<void(const MigrationInfo&)>;
    using GetSocketCallback = std::function<int32_t()>;
    using SetMigrationSocketCallback = std::function<void(int32_t)>;

    PathManager(std::shared_ptr<::quicx::common::IEventLoop> event_loop,
                SendManager& send_manager,
                ConnectionIDCoordinator& cid_coordinator,
                TransportParam& transport_param,
                ::quicx::common::Address& peer_addr,
                ToSendFrameCallback to_send_frame_cb,
                ActiveSendCallback active_send_cb,
                SetPeerAddressCallback set_peer_addr_cb);

    ~PathManager();

    // ==================== Path Validation ====================

    /**
     * @brief Start path validation probe to candidate address
     * Sends PATH_CHALLENGE and schedules retries
     */
    void StartPathValidationProbe();

    /**
     * @brief Start path validation probe with DCID pre-rotated
     * Used by InitiateMigration() when DCID has already been rotated
     * Skips CID rotation in OnPathResponse()
     */
    void StartPathValidationProbeWithPreRotation();

    /**
     * @brief Start probing next address in queue
     * Pops next candidate from queue and starts validation
     */
    void StartNextPathProbe();

    /**
     * @brief Handle PATH_RESPONSE frame
     * Validates token and promotes candidate to active path if matched
     * @param data Challenge data from PATH_RESPONSE
     */
    void OnPathResponse(const uint8_t* data);

    /**
     * @brief Handle PATH_CHALLENGE frame and generate response
     * @param data Challenge data to echo back
     * @param response_frame Output parameter for PATH_RESPONSE frame
     */
    void OnPathChallenge(const uint8_t* data, std::shared_ptr<IFrame>& response_frame);

    // ==================== Observed Address Handling ====================

    /**
     * @brief Handle observed peer address change (potential migration/NAT rebinding)
     * @param addr New observed peer address
     */
    void OnObservedPeerAddress(const ::quicx::common::Address& addr);

    /**
     * @brief Record bytes received from candidate path (for anti-amp budget)
     * @param bytes Number of bytes received
     */
    void OnCandidatePathBytesReceived(uint32_t bytes);

    // ==================== Client-Initiated Migration (Production API) ====================

    /**
     * @brief Initiate migration to a new local address
     * 
     * Creates a new socket, binds to the specified address, rotates DCID,
     * and starts path validation. This is the production-grade API.
     *
     * @param local_addr New local address to migrate to
     * @return MigrationResult indicating success or failure reason
     */
    MigrationResult InitiateMigrationToAddress(const ::quicx::common::Address& local_addr);

    /**
     * @brief Set callback for migration completion events
     * @param cb Callback invoked when migration completes (success or failure)
     */
    void SetMigrationCompleteCallback(MigrationCompleteCallback cb) { migration_complete_cb_ = cb; }

    /**
     * @brief Set callbacks for socket management during migration
     */
    void SetSocketCallbacks(GetSocketCallback get_sock_cb, SetMigrationSocketCallback set_migration_sock_cb) {
        get_socket_cb_ = get_sock_cb;
        set_migration_socket_cb_ = set_migration_sock_cb;
    }

    // ==================== Anti-Amplification ====================

    /**
     * @brief Enter anti-amplification state (restrict sending while path unvalidated)
     */
    void EnterAntiAmplification();

    /**
     * @brief Exit anti-amplification state (path validated, allow normal sending)
     */
    void ExitAntiAmplification();

    // ==================== Send Address Selection ====================

    /**
     * @brief Get address to send packets to
     * Returns candidate address if probing, otherwise active peer address
     * @return Address to use for sending
     */
    ::quicx::common::Address GetSendAddress() const;

    /**
     * @brief Get socket to use for sending
     * Returns migration socket if migration in progress, otherwise default
     * @return Socket fd to use for sending
     */
    int32_t GetSendSocket() const;

    // ==================== State Queries ====================

    /**
     * @brief Check if path probe is in flight
     * @return true if currently probing a path
     */
    bool IsPathProbeInflight() const { return path_probe_inflight_; }

    /**
     * @brief Get candidate peer address
     * @return Current candidate address being validated
     */
    const ::quicx::common::Address& GetCandidatePeerAddress() const { return candidate_peer_addr_; }

    /**
     * @brief Check if this is a client-initiated migration (not NAT rebinding)
     * @return true if current probe is for client-initiated migration
     */
    bool IsClientInitiatedMigration() const { return is_client_initiated_migration_; }

private:
    /**
     * @brief Internal implementation for starting path validation probe
     * @param dcid_pre_rotated true if DCID was already rotated by caller (InitiateMigration)
     */
    void StartPathValidationProbeInternal(bool dcid_pre_rotated);

    /**
     * @brief Schedule retry of path probe
     * Uses exponential backoff with max retries
     */
    void ScheduleProbeRetry();

    /**
     * @brief Complete migration: switch sockets and notify callback
     */
    void CompleteMigration();

    /**
     * @brief Handle migration failure: cleanup and notify callback
     * @param result The failure reason
     */
    void HandleMigrationFailure(MigrationResult result);

    /**
     * @brief Cleanup migration state (socket, timers, etc.)
     */
    void CleanupMigrationState();

    /**
     * @brief Create a new UDP socket bound to the specified address
     * @param local_addr Address to bind to
     * @return Socket fd on success, -1 on failure
     */
    int32_t CreateBoundSocket(const ::quicx::common::Address& local_addr);

private:
    // Dependencies (injected)
    std::shared_ptr<::quicx::common::IEventLoop> event_loop_;
    SendManager& send_manager_;
    ConnectionIDCoordinator& cid_coordinator_;
    TransportParam& transport_param_;
    ::quicx::common::Address& peer_addr_;  // Reference to main connection address
    ToSendFrameCallback to_send_frame_cb_;
    ActiveSendCallback active_send_cb_;
    SetPeerAddressCallback set_peer_addr_cb_;
    MigrationCompleteCallback migration_complete_cb_;
    GetSocketCallback get_socket_cb_;
    SetMigrationSocketCallback set_migration_socket_cb_;

    // Path validation state
    ::quicx::common::Address candidate_peer_addr_;
    bool path_probe_inflight_{false};
    uint8_t pending_path_challenge_data_[8]{0};
    ::quicx::common::TimerTask path_probe_task_;
    uint32_t probe_retry_count_{0};
    uint32_t probe_retry_delay_ms_{0};

    // Queue of pending candidate addresses for path validation
    std::vector<::quicx::common::Address> pending_candidate_addrs_;

    // Probe retry limits
    static constexpr uint32_t kMaxProbeRetries = 5;
    static constexpr uint32_t kInitialProbeDelayMs = 100;
    static constexpr uint32_t kMaxProbeDelayMs = 2000;

    // Flag: true if DCID was pre-rotated before starting probe (for client-initiated migration)
    // When true, OnPathResponse() will skip CID rotation (already done)
    bool dcid_pre_rotated_{false};

    // ==================== Client-Initiated Migration State ====================
    
    // True if current probe is for client-initiated migration (not NAT rebinding)
    bool is_client_initiated_migration_{false};
    
    // Socket created for migration (will become main socket on success)
    int32_t migration_socket_{-1};
    
    // Local address before migration (for reporting)
    ::quicx::common::Address old_local_addr_;
    
    // New local address for migration
    ::quicx::common::Address new_local_addr_;
    
    // Migration start time (for timeout and reporting)
    uint64_t migration_start_time_{0};
    
    // Migration timeout timer
    ::quicx::common::TimerTask migration_timeout_task_;
    
    // Path validation timeout (configurable)
    uint32_t path_validation_timeout_ms_{6000};
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_PATH_MANAGER_H
