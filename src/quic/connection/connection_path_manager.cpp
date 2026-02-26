
#include <algorithm>
#include <cstdint>
#include <cstring>

#include "common/log/log.h"
#include "common/util/time.h"

#include "common/network/if_event_loop.h"
#include "common/network/io_handle.h"
#include "quic/connection/connection_id_coordinator.h"
#include "quic/connection/connection_path_manager.h"
#include "quic/connection/controler/send_manager.h"
#include "quic/connection/transport_param.h"
#include "quic/frame/path_challenge_frame.h"
#include "quic/frame/path_response_frame.h"

namespace quicx {
namespace quic {

PathManager::PathManager(std::shared_ptr<::quicx::common::IEventLoop> event_loop, SendManager& send_manager,
    ConnectionIDCoordinator& cid_coordinator, TransportParam& transport_param, ::quicx::common::Address& peer_addr,
    ToSendFrameCallback to_send_frame_cb, ActiveSendCallback active_send_cb, SetPeerAddressCallback set_peer_addr_cb):
    event_loop_(event_loop),
    send_manager_(send_manager),
    cid_coordinator_(cid_coordinator),
    transport_param_(transport_param),
    peer_addr_(peer_addr),
    to_send_frame_cb_(to_send_frame_cb),
    active_send_cb_(active_send_cb),
    set_peer_addr_cb_(set_peer_addr_cb),
    path_probe_inflight_(false),
    probe_retry_count_(0),
    probe_retry_delay_ms_(0),
    is_client_initiated_migration_(false),
    migration_socket_(-1),
    migration_start_time_(0),
    path_validation_timeout_ms_(6000) {
    memset(pending_path_challenge_data_, 0, sizeof(pending_path_challenge_data_));
}

PathManager::~PathManager() {
    // Cleanup any pending migration socket
    CleanupMigrationState();
}

// ==================== Path Validation ====================

void PathManager::StartPathValidationProbe() {
    StartPathValidationProbeInternal(false);
}

void PathManager::StartPathValidationProbeWithPreRotation() {
    StartPathValidationProbeInternal(true);
}

void PathManager::StartPathValidationProbeInternal(bool dcid_pre_rotated) {
    if (path_probe_inflight_) {
        return;
    }

    // PATH_CHALLENGE can only be sent in 1-RTT packets, so Application keys must be ready
    // If not ready yet, the probe will be triggered later when OnTransportParams completes
    // This check should be done by caller (BaseConnection)

    // Record if DCID was pre-rotated (for client-initiated migration via InitiateMigration())
    dcid_pre_rotated_ = dcid_pre_rotated;

    // Generate PATH_CHALLENGE
    auto challenge = std::make_shared<PathChallengeFrame>();
    challenge->MakeData();
    memcpy(pending_path_challenge_data_, challenge->GetData(), 8);
    path_probe_inflight_ = true;

    EnterAntiAmplification();
    // Reset anti-amplification budget on send manager
    send_manager_.ResetAmpBudget();

    to_send_frame_cb_(challenge);

    probe_retry_count_ = 0;
    probe_retry_delay_ms_ = kInitialProbeDelayMs;
    ScheduleProbeRetry();

    // For client-initiated migration, also set a global timeout
    if (is_client_initiated_migration_) {
        event_loop_->RemoveTimer(migration_timeout_task_);
        migration_timeout_task_.SetTimeoutCallback([this]() {
            if (path_probe_inflight_ && is_client_initiated_migration_) {
                common::LOG_WARN("PathManager: migration timeout after %u ms", path_validation_timeout_ms_);
                HandleMigrationFailure(MigrationResult::kFailedTimeout);
            }
        });
        event_loop_->AddTimer(migration_timeout_task_, path_validation_timeout_ms_, 0);
    }
}

void PathManager::StartNextPathProbe() {
    // Check if there are pending addresses to probe
    if (pending_candidate_addrs_.empty()) {
        return;
    }

    // Get next address from queue
    candidate_peer_addr_ = pending_candidate_addrs_.front();
    pending_candidate_addrs_.erase(pending_candidate_addrs_.begin());

    common::LOG_INFO("PathManager: starting next path probe from queue to %s:%d (remaining in queue: %zu)",
        candidate_peer_addr_.GetIp().c_str(), candidate_peer_addr_.GetPort(), pending_candidate_addrs_.size());

    StartPathValidationProbe();
}

void PathManager::OnPathResponse(const uint8_t* data) {
    if (!path_probe_inflight_) {
        return;
    }

    if (memcmp(data, pending_path_challenge_data_, 8) != 0) {
        common::LOG_WARN("PathManager: PATH_RESPONSE token mismatch, ignoring");
        return;
    }

    // Token matched: path validated -> promote candidate to active
    path_probe_inflight_ = false;
    event_loop_->RemoveTimer(path_probe_task_);         // Cancel retry timer
    event_loop_->RemoveTimer(migration_timeout_task_);  // Cancel migration timeout
    memset(pending_path_challenge_data_, 0, sizeof(pending_path_challenge_data_));

    bool peer_addr_changed = !(candidate_peer_addr_ == peer_addr_);

    if (peer_addr_changed) {
        common::LOG_INFO("PathManager: path validated successfully, switching from %s:%d to %s:%d",
            peer_addr_.GetIp().c_str(), peer_addr_.GetPort(), candidate_peer_addr_.GetIp().c_str(),
            candidate_peer_addr_.GetPort());

        set_peer_addr_cb_(candidate_peer_addr_);

        // Rotate to next remote CID and retire the old one (delegated to coordinator)
        // SKIP if DCID was pre-rotated (client-initiated migration via InitiateMigration())
        if (!dcid_pre_rotated_) {
            cid_coordinator_.RotateRemoteConnectionID();
        } else {
            common::LOG_DEBUG("PathManager: skipping CID rotation (already pre-rotated)");
        }

        // Reset cwnd/RTT and PMTU for new path
        send_manager_.ResetPathSignals();
        send_manager_.ResetMtuForNewPath();
        // Kick off a minimal PMTU probe sequence on the new path
        send_manager_.StartMtuProbe();

        ExitAntiAmplification();
    }

    // Handle client-initiated migration completion
    if (is_client_initiated_migration_) {
        CompleteMigration();
    }

    // Candidate consumed
    candidate_peer_addr_ = ::quicx::common::Address();
    dcid_pre_rotated_ = false;  // Reset flag for next probe
    is_client_initiated_migration_ = false;

    // Check and replenish local CID pool after successful migration
    cid_coordinator_.CheckAndReplenishLocalCIDPool();

    // Start probing next address in queue if any
    StartNextPathProbe();
}

void PathManager::OnPathChallenge(const uint8_t* data, std::shared_ptr<IFrame>& response_frame) {
    auto response = std::make_shared<PathResponseFrame>();
    response->SetData((uint8_t*)data);
    response_frame = response;
}

// ==================== Observed Address Handling ====================

void PathManager::OnObservedPeerAddress(const ::quicx::common::Address& addr) {
    if (addr == peer_addr_) {
        return;
    }

    common::LOG_INFO("PathManager: observed new peer address: %s:%d (current: %s:%d)", addr.GetIp().c_str(),
        addr.GetPort(), peer_addr_.GetIp().c_str(), peer_addr_.GetPort());

    // Respect disable_active_migration: ignore proactive migration but allow NAT rebinding
    // Heuristic: if we have received any packet from the new address (workers call
    // OnCandidatePathDatagramReceived before this frame processing), it's likely NAT rebinding.
    if (transport_param_.GetDisableActiveMigration()) {
        // Only consider as NAT rebinding if we see repeated observations; otherwise ignore
        if (!(addr == candidate_peer_addr_)) {
            // First observation: store candidate but do not start probe yet
            common::LOG_DEBUG(
                "PathManager: first observation of new address (migration disabled), waiting for confirmation");
            candidate_peer_addr_ = addr;
            return;
        }
        // Second consecutive observation of same new address: treat as rebinding and probe
        common::LOG_INFO("PathManager: second observation confirmed, treating as NAT rebinding");
    }

    // Check if this address is already in the queue or currently being probed
    if (path_probe_inflight_ && addr == candidate_peer_addr_) {
        common::LOG_DEBUG(
            "PathManager: address %s:%d is already being probed, ignoring", addr.GetIp().c_str(), addr.GetPort());
        return;
    }

    for (const auto& pending : pending_candidate_addrs_) {
        if (addr == pending) {
            common::LOG_DEBUG(
                "PathManager: address %s:%d already in probe queue, ignoring", addr.GetIp().c_str(), addr.GetPort());
            return;
        }
    }

    // If probe is in progress, add to queue; otherwise start immediately
    if (path_probe_inflight_) {
        pending_candidate_addrs_.push_back(addr);
        common::LOG_INFO("PathManager: added %s:%d to probe queue (queue size: %zu)", addr.GetIp().c_str(),
            addr.GetPort(), pending_candidate_addrs_.size());
    } else {
        candidate_peer_addr_ = addr;
        StartPathValidationProbe();

        // If probe didn't start (e.g., Application keys not ready), queue the address for later
        if (!path_probe_inflight_) {
            pending_candidate_addrs_.push_back(addr);
            common::LOG_INFO("PathManager: path probe deferred, added %s:%d to queue (queue size: %zu)",
                addr.GetIp().c_str(), addr.GetPort(), pending_candidate_addrs_.size());
        } else {
            common::LOG_INFO(
                "PathManager: started path validation probe to %s:%d", addr.GetIp().c_str(), addr.GetPort());
        }
    }
}

void PathManager::OnCandidatePathBytesReceived(uint32_t bytes) {
    if (path_probe_inflight_) {
        send_manager_.OnCandidatePathBytesReceived(bytes);
    }
}

// ==================== Client-Initiated Migration ====================

MigrationResult PathManager::InitiateMigrationToAddress(const ::quicx::common::Address& local_addr) {
    common::LOG_INFO("PathManager::InitiateMigrationToAddress: starting migration to local %s:%d",
        local_addr.GetIp().c_str(), local_addr.GetPort());

    // 1. Check if migration is disabled by peer
    if (transport_param_.GetDisableActiveMigration()) {
        common::LOG_WARN("PathManager: migration disabled by peer");
        return MigrationResult::kFailedMigrationDisabled;
    }

    // 2. Check if probe is already in progress
    if (path_probe_inflight_) {
        common::LOG_WARN("PathManager: probe already in progress");
        return MigrationResult::kFailedProbeInProgress;
    }

    // 3. Pre-rotate DCID before starting migration
    if (!cid_coordinator_.RotateRemoteConnectionID()) {
        common::LOG_WARN("PathManager: failed to rotate DCID (no available CID from peer)");
        return MigrationResult::kFailedNoAvailableCID;
    }

    // 4. Create new socket bound to the specified local address
    int32_t new_socket = CreateBoundSocket(local_addr);
    if (new_socket < 0) {
        // Rotation already happened, but we failed. This is bad but we continue with old socket.
        common::LOG_ERROR("PathManager: failed to create socket for migration");
        return MigrationResult::kFailedSocketCreation;
    }

    // 5. Save old local address for reporting
    if (get_socket_cb_) {
        int32_t old_sock = get_socket_cb_();
        if (old_sock > 0) {
            common::ParseLocalAddress(old_sock, old_local_addr_);
        }
    }

    // 6. Store migration state
    migration_socket_ = new_socket;
    // new_local_addr_ is populated by CreateBoundSocket
    is_client_initiated_migration_ = true;
    migration_start_time_ = common::UTCTimeMsec();

    // 7. Set migration socket for sending during validation
    if (set_migration_socket_cb_) {
        set_migration_socket_cb_(new_socket);
    }

    // 8. Start path validation with pre-rotated DCID
    // Note: peer_addr_ is unchanged, we're just changing our local address
    candidate_peer_addr_ = peer_addr_;  // Same peer, new local path

    common::LOG_INFO("PathManager: migration initiated, old local: %s:%d, new local: %s:%d, peer: %s:%d",
        old_local_addr_.GetIp().c_str(), old_local_addr_.GetPort(), new_local_addr_.GetIp().c_str(),
        new_local_addr_.GetPort(), peer_addr_.GetIp().c_str(), peer_addr_.GetPort());

    StartPathValidationProbeInternal(true);  // DCID already pre-rotated

    return MigrationResult::kSuccess;
}

// ==================== Anti-Amplification ====================

void PathManager::EnterAntiAmplification() {
    // Disable streams while path is unvalidated to limit to probing/ACK frames
    send_manager_.SetStreamsAllowed(false);
}

void PathManager::ExitAntiAmplification() {
    send_manager_.SetStreamsAllowed(true);
}

// ==================== Send Address Selection ====================

::quicx::common::Address PathManager::GetSendAddress() const {
    // For now, always send to active peer address. Candidate is used for validation step later.
    if (path_probe_inflight_) {
        return candidate_peer_addr_;
    }
    return peer_addr_;
}

int32_t PathManager::GetSendSocket() const {
    // During client-initiated migration, use the migration socket
    if (is_client_initiated_migration_ && migration_socket_ > 0) {
        return migration_socket_;
    }
    // Otherwise return -1 to indicate use default socket
    return -1;
}

// ==================== Private Methods ====================

void PathManager::ScheduleProbeRetry() {
    event_loop_->RemoveTimer(path_probe_task_);
    if (!path_probe_inflight_) {
        return;
    }

    if (probe_retry_count_ >= kMaxProbeRetries) {
        // Give up probing after max retries; revert to old path
        common::LOG_WARN(
            "PathManager: path validation failed after %d attempts, reverting to old path. candidate: %s:%d",
            probe_retry_count_, candidate_peer_addr_.GetIp().c_str(), candidate_peer_addr_.GetPort());

        if (is_client_initiated_migration_) {
            HandleMigrationFailure(MigrationResult::kFailedPathValidation);
        } else {
            // Clean up probe state
            path_probe_inflight_ = false;
            candidate_peer_addr_ = ::quicx::common::Address();
            memset(pending_path_challenge_data_, 0, sizeof(pending_path_challenge_data_));

            // Critical: restore stream sending capability
            ExitAntiAmplification();

            // Start probing next address in queue if any
            StartNextPathProbe();
        }
        return;
    }

    probe_retry_count_++;
    probe_retry_delay_ms_ = std::min<uint32_t>(probe_retry_delay_ms_ * 2, kMaxProbeDelayMs);

    path_probe_task_.SetTimeoutCallback([this]() {
        if (!path_probe_inflight_) {
            return;
        }
        auto challenge = std::make_shared<PathChallengeFrame>();
        challenge->MakeData();
        common::LOG_DEBUG("PathManager: retrying path validation (attempt %d/%d) to %s:%d", probe_retry_count_ + 1,
            kMaxProbeRetries, candidate_peer_addr_.GetIp().c_str(), candidate_peer_addr_.GetPort());
        memcpy(pending_path_challenge_data_, challenge->GetData(), 8);
        to_send_frame_cb_(challenge);
        ScheduleProbeRetry();
    });

    event_loop_->AddTimer(path_probe_task_, probe_retry_delay_ms_, 0);
}

void PathManager::CompleteMigration() {
    common::LOG_INFO("PathManager::CompleteMigration: migration successful, switching to new socket");

    // Migration successful: the new socket becomes the main socket
    // The connection should now use migration_socket_ as the primary socket

    // Build migration info for callback
    MigrationInfo info;
    info.old_local_ip_ = old_local_addr_.GetIp();
    info.old_local_port_ = old_local_addr_.GetPort();
    info.new_local_ip_ = new_local_addr_.GetIp();
    info.new_local_port_ = new_local_addr_.GetPort();
    info.old_peer_ip_ = peer_addr_.GetIp();
    info.old_peer_port_ = peer_addr_.GetPort();
    info.new_peer_ip_ = peer_addr_.GetIp();
    info.new_peer_port_ = peer_addr_.GetPort();
    info.migration_start_time_ = migration_start_time_;
    info.migration_end_time_ = common::UTCTimeMsec();
    info.result_ = MigrationResult::kSuccess;
    info.is_nat_rebinding_ = false;

    // The migration socket is now the main socket
    // The old socket should be closed by the caller after switching
    // We keep migration_socket_ set so that GetSendSocket() continues to return it
    // until the BaseConnection switches the socket

    // Invoke callback to notify application layer
    if (migration_complete_cb_) {
        migration_complete_cb_(info);
    }

    // Note: We don't close migration_socket_ here because it's now the active socket
    // The caller (BaseConnection) is responsible for:
    // 1. Using migration_socket_ as the new primary socket
    // 2. Closing the old socket
    // 3. Clearing migration_socket_ after the switch

    common::LOG_INFO("PathManager: migration completed successfully in %lu ms",
        info.migration_end_time_ - info.migration_start_time_);
}

void PathManager::HandleMigrationFailure(MigrationResult result) {
    common::LOG_WARN("PathManager::HandleMigrationFailure: migration failed with result %d", static_cast<int>(result));

    // Build migration info for callback
    MigrationInfo info;
    info.old_local_ip_ = old_local_addr_.GetIp();
    info.old_local_port_ = old_local_addr_.GetPort();
    info.new_local_ip_ = new_local_addr_.GetIp();
    info.new_local_port_ = new_local_addr_.GetPort();
    info.old_peer_ip_ = peer_addr_.GetIp();
    info.old_peer_port_ = peer_addr_.GetPort();
    info.new_peer_ip_ = peer_addr_.GetIp();
    info.new_peer_port_ = peer_addr_.GetPort();
    info.migration_start_time_ = migration_start_time_;
    info.migration_end_time_ = common::UTCTimeMsec();
    info.result_ = result;
    info.is_nat_rebinding_ = false;

    // Cleanup migration state
    CleanupMigrationState();

    // Clean up probe state
    path_probe_inflight_ = false;
    candidate_peer_addr_ = ::quicx::common::Address();
    memset(pending_path_challenge_data_, 0, sizeof(pending_path_challenge_data_));
    dcid_pre_rotated_ = false;
    is_client_initiated_migration_ = false;

    // Restore stream sending capability
    ExitAntiAmplification();

    // Invoke callback to notify application layer
    if (migration_complete_cb_) {
        migration_complete_cb_(info);
    }

    // Start probing next address in queue if any
    StartNextPathProbe();
}

void PathManager::CleanupMigrationState() {
    // Cancel timers
    event_loop_->RemoveTimer(migration_timeout_task_);

    // Close and cleanup migration socket if migration failed
        if (migration_socket_ > 0) {
        // Only close if migration failed; if successful, the socket is now in use
        if (!is_client_initiated_migration_ || path_probe_inflight_) {
            // Migration in progress but we're cleaning up = failure
            common::Close(migration_socket_);
        }
        migration_socket_ = -1;
    }

    // Clear migration socket callback
    if (set_migration_socket_cb_) {
        set_migration_socket_cb_(-1);
    }

    // Reset addresses
    old_local_addr_ = ::quicx::common::Address();
    new_local_addr_ = ::quicx::common::Address();
    migration_start_time_ = 0;
}

int32_t PathManager::CreateBoundSocket(const ::quicx::common::Address& local_addr) {
    // Create UDP socket
    auto sock_ret = common::UdpSocket();
    if (sock_ret.errno_ != 0) {
        common::LOG_ERROR("PathManager: failed to create UDP socket: errno=%d", sock_ret.errno_);
        return -1;
    }

    int32_t sockfd = sock_ret.return_value_;

    // Set non-blocking
    common::SocketNoblocking(sockfd);

    // Bind to specified address
    common::Address bind_addr = local_addr;
    auto bind_ret = common::Bind(sockfd, bind_addr);
        if (bind_ret.errno_ != 0) {
        common::LOG_ERROR("PathManager: failed to bind socket to %s:%d: errno=%d", local_addr.GetIp().c_str(),
            local_addr.GetPort(), bind_ret.errno_);
        common::Close(sockfd);
        return -1;
    }

    // Get the actual assigned port and address
    if (!common::ParseLocalAddress(sockfd, new_local_addr_)) {
        new_local_addr_ = bind_addr;
    }

    common::LOG_INFO("PathManager: created migration socket %d bound to %s:%d", sockfd, new_local_addr_.GetIp().c_str(),
        new_local_addr_.GetPort());

    return sockfd;
}

}  // namespace quic
}  // namespace quicx
