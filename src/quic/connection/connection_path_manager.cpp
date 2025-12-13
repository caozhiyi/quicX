
#include <algorithm>
#include <cstdint>
#include <cstring>

#include "common/log/log.h"

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
    probe_retry_delay_ms_(0) {
    memset(pending_path_challenge_data_, 0, sizeof(pending_path_challenge_data_));
}

// ==================== Path Validation ====================

void PathManager::StartPathValidationProbe() {
    if (path_probe_inflight_) {
        return;
    }

    // PATH_CHALLENGE can only be sent in 1-RTT packets, so Application keys must be ready
    // If not ready yet, the probe will be triggered later when OnTransportParams completes
    // This check should be done by caller (BaseConnection)

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
    event_loop_->RemoveTimer(path_probe_task_);  // Cancel retry timer
    memset(pending_path_challenge_data_, 0, sizeof(pending_path_challenge_data_));

    if (!(candidate_peer_addr_ == peer_addr_)) {
        common::LOG_INFO("PathManager: path validated successfully, switching from %s:%d to %s:%d",
            peer_addr_.GetIp().c_str(), peer_addr_.GetPort(), candidate_peer_addr_.GetIp().c_str(),
            candidate_peer_addr_.GetPort());

        set_peer_addr_cb_(candidate_peer_addr_);

        // Rotate to next remote CID and retire the old one (delegated to coordinator)
        cid_coordinator_.RotateRemoteConnectionID();

        // Reset cwnd/RTT and PMTU for new path
        send_manager_.ResetPathSignals();
        send_manager_.ResetMtuForNewPath();
        // Kick off a minimal PMTU probe sequence on the new path
        send_manager_.StartMtuProbe();

        ExitAntiAmplification();
    }

    // Candidate consumed
    candidate_peer_addr_ = ::quicx::common::Address();

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

        // Clean up probe state
        path_probe_inflight_ = false;
        candidate_peer_addr_ = ::quicx::common::Address();
        memset(pending_path_challenge_data_, 0, sizeof(pending_path_challenge_data_));

        // Critical: restore stream sending capability
        ExitAntiAmplification();

        // Start probing next address in queue if any
        StartNextPathProbe();
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

}  // namespace quic
}  // namespace quicx
