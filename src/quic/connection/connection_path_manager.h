#ifndef QUIC_CONNECTION_PATH_MANAGER_H
#define QUIC_CONNECTION_PATH_MANAGER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <quicx/quic/type.h>
#include "quic/connection/type.h"
#include "common/network/address.h"
#include <quicx/common/if_timer_scheduler.h>
#include "quic/common/constants.h"

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
    // Sends a probe frame synchronously as a standalone 1-RTT packet that
    // bypasses the send queue. Used for PATH_CHALLENGE so it becomes the
    // first packet on the new path (interop runners check this). Returning
    // false means "cannot send now" and the caller falls back to queueing.
    using SendProbeNowCallback = std::function<bool(std::shared_ptr<IFrame>)>;
    using ActiveSendCallback = std::function<void()>;
    using SetPeerAddressCallback = std::function<void(const ::quicx::common::Address&)>;
    using MigrationCompleteCallback = std::function<void(const MigrationInfo&)>;
    using GetSocketCallback = std::function<int32_t()>;
    // Hands the freshly created probe fd to its owner (MigrationController).
    using ProbeSocketReadyCallback = std::function<void(int32_t)>;

    /**
     * @brief Construction-time dependencies for PathManager.
     *
     * Bundled into a single struct (instead of an 8-argument constructor)
     * so that callers initialize fields by name. This makes the wiring at
     * the construction site self-documenting and removes the silent
     * positional-argument hazard when adding/removing dependencies.
     *
     * Lifetime contract:
     *  - `event_loop` is held as `weak_ptr` (no lifetime bump).
     *  - `send_manager`, `cid_coordinator`, `transport_param`, `peer_addr`
     *    are stored as references and MUST outlive the PathManager.
     *  - The callbacks are copied (std::function); they capture
     *    `BaseConnection*` via `this`, so the connection must outlive the
     *    PathManager (it does — PathManager is a member of BaseConnection).
     */
    struct Deps {
        std::shared_ptr<::quicx::common::IEventLoop> event_loop;
        SendManager* send_manager{nullptr};
        ConnectionIDCoordinator* cid_coordinator{nullptr};
        TransportParam* transport_param{nullptr};
        ::quicx::common::Address* peer_addr{nullptr};
        ToSendFrameCallback to_send_frame_cb;
        SendProbeNowCallback send_probe_now_cb;
        ActiveSendCallback active_send_cb;
        SetPeerAddressCallback set_peer_addr_cb;
    };

    explicit PathManager(Deps deps);

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
     * @brief Initiate migration to a new PEER address (preferred-address flow)
     *
     * RFC 9000 §9.6: the server may advertise a preferred_address; the client
     * migrates by sending to that address. Unlike InitiateMigrationToAddress()
     * (new local socket, same peer), this rotates the peer address — which may
     * also change address family (e.g. IPv6 -> IPv4), so a socket matching the
     * TARGET family is created. The DCID is pre-rotated so the very first
     * packet on the new path already uses a fresh connection ID (§9.5, and the
     * interop connectionmigration check requires exactly that).
     *
     * @param peer_addr Server's preferred address to migrate to
     * @return MigrationResult indicating success or failure reason
     */
    MigrationResult InitiateMigrationToPeerAddress(const ::quicx::common::Address& peer_addr);

    /**
     * @brief Supply the connection ID carried in the server's preferred_address
     * transport parameter (RFC 9000 §18.2). When set, the next preferred-address
     * migration uses this CID as the new DCID instead of rotating to a pooled
     * NEW_CONNECTION_ID CID (which a server need not provide alongside the
     * preferred address).
     */
    void SetPreferredConnectionID(const uint8_t* cid, uint16_t len);

    /**
     * @brief Set callback for migration completion events
     * @param cb Callback invoked when migration completes (success or failure)
     */
    void SetMigrationCompleteCallback(MigrationCompleteCallback cb) { migration_complete_cb_ = cb; }

    /**
     * @brief Wire up socket handling for migration.
     *
     * PathManager creates the probe socket but does not own it: the fd is handed
     * straight to on_probe_socket_ready and never stored here. That keeps
     * DatagramEmitter the single knower of the active fd and MigrationController
     * the single owner of its lifecycle — previously this class kept a second
     * copy (migration_socket_) synchronised by callback, and that copy was the
     * one that got closed twice on a failed migration.
     *
     * @param get_active_socket     Returns the currently active fd (used to read
     *                              the old local address for reporting).
     * @param on_probe_socket_ready Receives the freshly created probe fd.
     */
    void SetSocketFactoryCallbacks(
        GetSocketCallback get_active_socket, ProbeSocketReadyCallback on_probe_socket_ready) {
        get_socket_cb_ = std::move(get_active_socket);
        probe_socket_ready_cb_ = std::move(on_probe_socket_ready);
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
     * @param force_ipv4 When set, forces an IPv4 socket regardless of the
     *                   current peer's family (needed when migrating to a
     *                   preferred address of a different family, e.g. IPv6 ->
     *                   IPv4). Unset = derive the family from the current peer.
     * @return Socket fd on success, -1 on failure
     */
    int32_t CreateBoundSocket(const ::quicx::common::Address& local_addr, std::optional<bool> force_ipv4 = std::nullopt);

private:
    // Dependencies (injected)
    std::weak_ptr<::quicx::common::IEventLoop> event_loop_;
    SendManager& send_manager_;
    ConnectionIDCoordinator& cid_coordinator_;
    TransportParam& transport_param_;
    ::quicx::common::Address& peer_addr_;  // Reference to main connection address
    ToSendFrameCallback to_send_frame_cb_;
    SendProbeNowCallback send_probe_now_cb_;
    ActiveSendCallback active_send_cb_;
    SetPeerAddressCallback set_peer_addr_cb_;
    MigrationCompleteCallback migration_complete_cb_;
    GetSocketCallback get_socket_cb_;
    ProbeSocketReadyCallback probe_socket_ready_cb_;

    // Path validation state
    ::quicx::common::Address candidate_peer_addr_;
    bool path_probe_inflight_{false};
    uint8_t pending_path_challenge_data_[8]{0};
    ::quicx::common::Timer path_probe_task_;
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

    // Peer endpoint immediately BEFORE the last successful path switch
    // (captured in OnPathResponse before set_peer_addr_cb_ rewrites
    // peer_addr_); CompleteMigration() reports it as MigrationInfo::old_peer.
    ::quicx::common::Address last_old_peer_addr_;

    // ==================== Client-Initiated Migration State ====================

    // True if current probe is for client-initiated migration (not NAT rebinding)
    bool is_client_initiated_migration_{false};

    // NB: the probe socket fd is deliberately NOT stored here. It is handed to
    // MigrationController via probe_socket_ready_cb_ the moment it is created,
    // so there is exactly one owner and exactly one close(2) site.

    // Local address before migration (for reporting)
    ::quicx::common::Address old_local_addr_;

    // New local address for migration
    ::quicx::common::Address new_local_addr_;

    // Migration start time (for timeout and reporting)
    uint64_t migration_start_time_{0};

    // Migration timeout timer
    ::quicx::common::Timer migration_timeout_task_;
    // Guards both timer callbacks above, which capture a raw `this`.
    std::shared_ptr<int> life_token_ = std::make_shared<int>(0);

    // Path validation timeout (configurable; default per RFC 9000 §8.2.4
    // — at least 3×PTO, see kDefaultPathValidationTimeoutMs in
    // quic/common/constants.h).
    uint32_t path_validation_timeout_ms_{kDefaultPathValidationTimeoutMs};

    // Preferred-address CID (RFC 9000 §9.6): the DCID to use on the migrated path.
    bool has_preferred_cid_{false};
    uint8_t preferred_cid_bytes_[kMaxCidLength]{0};
    uint16_t preferred_cid_len_{0};
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_PATH_MANAGER_H
