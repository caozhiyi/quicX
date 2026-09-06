#include <algorithm>
#include <cstdint>
#include <cstring>

#include <openssl/mem.h>

#include "common/log/log.h"
#include "common/network/if_event_loop.h"
#include "common/network/io_handle.h"
#include "common/util/time.h"

#include "quic/connection/connection_id_coordinator.h"
#include "quic/connection/connection_path_manager.h"
#include "quic/connection/controller/send_manager.h"
#include "quic/connection/transport_param.h"
#include "quic/frame/path_challenge_frame.h"
#include "quic/frame/path_response_frame.h"

namespace quicx {
namespace quic {

PathManager::PathManager(Deps deps):
    event_loop_(deps.event_loop),
    send_manager_(*deps.send_manager),
    cid_coordinator_(*deps.cid_coordinator),
    transport_param_(*deps.transport_param),
    peer_addr_(*deps.peer_addr),
    to_send_frame_cb_(std::move(deps.to_send_frame_cb)),
    send_probe_now_cb_(std::move(deps.send_probe_now_cb)),
    active_send_cb_(std::move(deps.active_send_cb)),
    set_peer_addr_cb_(std::move(deps.set_peer_addr_cb)),
    path_probe_inflight_(false),
    probe_retry_count_(0),
    probe_retry_delay_ms_(0),
    is_client_initiated_migration_(false),
    migration_start_time_(0),
    path_validation_timeout_ms_(kDefaultPathValidationTimeoutMs) {
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

void PathManager::StartPathValidationProbeInternal(bool dcid_pre_rotated, bool probe_state_pre_set) {
    // Allow continuation when path_probe_inflight_ was pre-set by
    // InitiateMigrationToPeerAddress to prevent queued-packet leakage
    // to the old peer address during the socket handoff window.
    if (!probe_state_pre_set && path_probe_inflight_) {
        return;
    }

    // PATH_CHALLENGE can only be sent in 1-RTT packets, so Application keys must be ready
    // If not ready yet, the probe will be triggered later when OnTransportParams completes
    // This check should be done by caller (BaseConnection)

    // Record if DCID was pre-rotated (for client-initiated migration via InitiateMigration())
    dcid_pre_rotated_ = dcid_pre_rotated;

    // Generate PATH_CHALLENGE
    auto challenge = std::make_shared<PathChallengeFrame>();
    if (!challenge->MakeData()) {
        // Without an unpredictable token the probe proves nothing, so do not send one.
        // Known edge (accepted): when reached from InitiateMigrationToPeer*
        // the probe socket is already handed off/registered and the migration
        // flags are set, leaving a stalled half-state until the connection
        // closes. Only reachable on RAND_bytes failure (~never); fixing it
        // would need a controller-level socket-revoke callback.
        LOG_ERROR("PathManager: cannot start path validation, failed to generate challenge token");
        return;
    }
    memcpy(pending_path_challenge_data_, challenge->GetData(), 8);
    if (!probe_state_pre_set) {
        path_probe_inflight_ = true;
        EnterAntiAmplification();
        send_manager_.ResetAmpBudget();
    }

    LOG_DEBUG("PathManager: path validation challenge ready (candidate %s:%d)", candidate_peer_addr_.GetIp().c_str(),
        candidate_peer_addr_.GetPort());
    // The PATH_CHALLENGE must be the first packet sent on the new path
    // (interop runners assert this; RFC 9000 §9 expects path validation
    // before the path carries data). Send it synchronously, bypassing the
    // send queue where already-queued stream/ACK frames would otherwise be
    // packed first. Fall back to queueing when an immediate send is not
    // possible (e.g. 1-RTT keys not yet installed).
    const bool sent_now = send_probe_now_cb_ && send_probe_now_cb_(challenge);
    if (!sent_now) {
        LOG_DEBUG("PathManager: immediate probe send unavailable, queueing challenge");
        to_send_frame_cb_(challenge);
    }

    probe_retry_count_ = 0;
    probe_retry_delay_ms_ = kInitialProbeDelayMs;
    ScheduleProbeRetry();

    // For client-initiated migration, also set a global timeout
    if (is_client_initiated_migration_) {
        auto loop = event_loop_.lock();
        if (loop) {
            migration_timeout_task_ = loop->AddTimer(
                life_token_,
                [this]() {
                    if (path_probe_inflight_ && is_client_initiated_migration_) {
                        LOG_WARN("PathManager: migration timeout after %u ms", path_validation_timeout_ms_);
                        HandleMigrationFailure(MigrationResult::kFailedTimeout);
                    }
                },
                path_validation_timeout_ms_);
        }
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

    LOG_INFO("PathManager: starting next path probe from queue to %s:%d (remaining in queue: %zu)",
        candidate_peer_addr_.GetIp().c_str(), candidate_peer_addr_.GetPort(), pending_candidate_addrs_.size());

    StartPathValidationProbe();
}

void PathManager::OnPathResponse(const uint8_t* data) {
    if (!path_probe_inflight_) {
        return;
    }

    // Constant-time compare: a timing side channel here would let an off-path
    // attacker recover the outstanding token byte by byte and forge a response.
    if (CRYPTO_memcmp(data, pending_path_challenge_data_, 8) != 0) {
        LOG_DEBUG("PathManager::OnPathResponse: token mismatch, ignoring");
        return;
    }

    // Token matched: path validated -> promote candidate to active
    path_probe_inflight_ = false;
    path_probe_task_.Cancel();         // Cancel retry timer
    migration_timeout_task_.Cancel();  // Cancel migration timeout
    memset(pending_path_challenge_data_, 0, sizeof(pending_path_challenge_data_));

    bool peer_addr_changed = !(candidate_peer_addr_ == peer_addr_);

    if (peer_addr_changed) {
        LOG_INFO("PathManager: path validated successfully, switching from %s:%d to %s:%d", peer_addr_.GetIp().c_str(),
            peer_addr_.GetPort(), candidate_peer_addr_.GetIp().c_str(), candidate_peer_addr_.GetPort());

        // Snapshot the OLD peer BEFORE set_peer_addr_cb_ rewrites peer_addr_:
        // CompleteMigration() reports old/new endpoints in MigrationInfo, and
        // peer_addr_ is already the new endpoint by the time it runs.
        last_old_peer_addr_ = peer_addr_;
        set_peer_addr_cb_(candidate_peer_addr_);

        // Rotate to next remote CID (delegated to coordinator). For client-initiated
        // migration the DCID was already pre-rotated in InitiateMigrationToAddress().
        if (!dcid_pre_rotated_) {
            cid_coordinator_.RotateRemoteConnectionID();
        } else {
            LOG_DEBUG("PathManager: skipping CID rotation (already pre-rotated)");
        }

        // The migration path is now validated, so it is safe to retire the
        // previously-used DCID. Retiring it earlier (in the PATH_CHALLENGE probe)
        // would make the peer delete a path that was still active and abort the
        // connection (RFC 9000 §9.2 / peer's path-deletion checks).
        cid_coordinator_.RetirePendingRemoteConnectionID();

        // Reset cwnd/RTT and PMTU for new path
        send_manager_.ResetPathSignals();
        send_manager_.ResetMtuForNewPath();
        // Kick off a minimal PMTU probe sequence on the new path
        send_manager_.StartMtuProbe();
    } else {
        // Local-only path change (client migrated to our preferred_address
        // listener): the path is still new per RFC 9000 §9.4, so congestion
        // and PMTU state must not carry over from the old one.
        send_manager_.ResetPathSignals();
        send_manager_.ResetMtuForNewPath();
        send_manager_.StartMtuProbe();
    }

    // A probe enters anti-amplification regardless of which address
    // component changed (peer address for NAT rebind / active migration,
    // local socket for a preferred-address migration). A validated path
    // always leaves it; keeping the exit inside the peer_addr_changed branch
    // would pin streams off forever on local-only changes.
    ExitAntiAmplification();

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

    // Respect disable_active_migration: ignore proactive migration but allow NAT rebinding
    // Heuristic: if we have received any packet from the new address (workers call
    // OnCandidatePathDatagramReceived before this frame processing), it's likely NAT rebinding.
    if (transport_param_.GetDisableActiveMigration()) {
        // Only consider as NAT rebinding if we see repeated observations; otherwise ignore
        if (!(addr == candidate_peer_addr_)) {
            // First observation: store candidate but do not start probe yet
            LOG_DEBUG("PathManager: first observation of new address (migration disabled), waiting for confirmation");
            candidate_peer_addr_ = addr;
            return;
        }
        // Second consecutive observation of same new address: treat as rebinding and probe
        LOG_INFO("PathManager: second observation confirmed, treating as NAT rebinding");
    }

    // Check if this address is already in the queue or currently being probed
    if (path_probe_inflight_ && addr == candidate_peer_addr_) {
        LOG_DEBUG("PathManager: address %s:%d is already being probed, ignoring", addr.GetIp().c_str(), addr.GetPort());
        return;
    }

    for (const auto& pending : pending_candidate_addrs_) {
        if (addr == pending) {
            LOG_DEBUG(
                "PathManager: address %s:%d already in probe queue, ignoring", addr.GetIp().c_str(), addr.GetPort());
            return;
        }
    }

    // If probe is in progress, add to queue; otherwise start immediately
    if (path_probe_inflight_) {
        pending_candidate_addrs_.push_back(addr);
        LOG_INFO("PathManager: added %s:%d to probe queue (queue size: %zu)", addr.GetIp().c_str(), addr.GetPort(),
            pending_candidate_addrs_.size());
    } else {
        candidate_peer_addr_ = addr;
        StartPathValidationProbe();

        // If probe didn't start (e.g., Application keys not ready), queue the address for later
        if (!path_probe_inflight_) {
            pending_candidate_addrs_.push_back(addr);
            LOG_INFO("PathManager: path probe deferred, added %s:%d to queue (queue size: %zu)", addr.GetIp().c_str(),
                addr.GetPort(), pending_candidate_addrs_.size());
        } else {
            LOG_INFO("PathManager: started path validation probe to %s:%d", addr.GetIp().c_str(), addr.GetPort());
        }
    }
}

void PathManager::OnCandidatePathBytesReceived(uint32_t bytes) {
    // Intentionally does not credit the amplification budget. Every received
    // datagram is now credited exactly once, centrally, in
    // BaseConnection::OnPackets(). Crediting again here would double-count bytes
    // arriving on a candidate path during migration and hand out twice the
    // budget RFC 9000 §8.1 allows.
    (void)bytes;
}

// ==================== Client-Initiated Migration ====================

MigrationResult PathManager::InitiateMigrationToAddress(const ::quicx::common::Address& local_addr) {
    LOG_INFO("PathManager::InitiateMigrationToAddress: starting migration to local %s:%d", local_addr.GetIp().c_str(),
        local_addr.GetPort());

    // 1. Check if migration is disabled by peer
    if (transport_param_.GetPeerDisableActiveMigration()) {
        LOG_WARN("PathManager: migration disabled by peer");
        return MigrationResult::kFailedMigrationDisabled;
    }

    // 2. Check if probe is already in progress
    if (path_probe_inflight_) {
        LOG_WARN("PathManager: probe already in progress");
        return MigrationResult::kFailedProbeInProgress;
    }

    // 3. Pre-rotate DCID per RFC 9000 §9.5 ("An endpoint SHOULD use a new
    //    connection ID when it [...] initiates connection migration").
    //    The official QUIC interop "connectionmigration" scenario validates that
    //    the first packet after migration uses a new DCID. We rotate BEFORE sending
    //    PATH_CHALLENGE so the new DCID is used from the first migrated packet.
    //    Note: the server must have provided additional CIDs via NEW_CONNECTION_ID
    //    frames (typically done at handshake completion); all such CIDs are already
    //    registered in the server's conn_map_, so routing is not an issue.
    bool dcid_rotated = cid_coordinator_.RotateRemoteConnectionID();
    if (!dcid_rotated) {
        LOG_WARN("PathManager: no available remote CID for migration, aborting");
        return MigrationResult::kFailedNoAvailableCID;
    }
    LOG_DEBUG("PathManager: pre-rotated DCID for client-initiated migration");

    // 4. Create new socket bound to the specified local address
    common::SocketHandle new_socket = CreateBoundSocket(local_addr);
    if (new_socket.fd < 0) {
        // Rotation already happened, but we failed. This is bad but we continue with old socket.
        LOG_ERROR("PathManager: failed to create socket for migration");
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
    // new_local_addr_ is populated by CreateBoundSocket
    is_client_initiated_migration_ = true;
    migration_start_time_ = common::UTCTimeMsec();

    // 7. Hand the probe socket to its owner (MigrationController), which
    // installs it on the emitter and registers it with the receiver. We keep no
    // copy: a second copy is exactly what caused the fd to be closed twice on a
    // failed migration.
    if (probe_socket_ready_cb_) {
        probe_socket_ready_cb_(new_socket);
    }

    // 8. Start path validation with pre-rotated DCID
    // Note: peer_addr_ is unchanged, we're just changing our local address
    candidate_peer_addr_ = peer_addr_;  // Same peer, new local path

    LOG_INFO("PathManager: migration initiated, old local: %s:%d, new local: %s:%d, peer: %s:%d",
        old_local_addr_.GetIp().c_str(), old_local_addr_.GetPort(), new_local_addr_.GetIp().c_str(),
        new_local_addr_.GetPort(), peer_addr_.GetIp().c_str(), peer_addr_.GetPort());

    StartPathValidationProbeInternal(dcid_rotated);  // Only skip CID rotation in OnPathResponse if actually pre-rotated

    return MigrationResult::kSuccess;
}

void PathManager::SetPreferredConnectionID(const uint8_t* cid, uint16_t len) {
    if (cid == nullptr || len == 0 || len > kMaxCidLength) {
        LOG_WARN("PathManager::SetPreferredConnectionID: invalid CID (len=%u), ignoring", len);
        has_preferred_cid_ = false;
        return;
    }
    memcpy(preferred_cid_bytes_, cid, len);
    preferred_cid_len_ = len;
    has_preferred_cid_ = true;
}

MigrationResult PathManager::InitiateMigrationToPeerAddress(const ::quicx::common::Address& peer_addr) {
    LOG_INFO("PathManager::InitiateMigrationToPeerAddress: migrating to peer %s:%d", peer_addr.GetIp().c_str(),
        peer_addr.GetPort());

    // Note: disable_active_migration is deliberately NOT checked here. RFC
    // 9000 §18.2 scopes that transport parameter to NAT-rebinding-induced
    // migration only; migrating to an advertised preferred_address is always
    // permitted. (Do not "fix" this into an InitiateMigrationToAddress-style
    // symmetry check.)

    // 1. Check if a probe is already in progress
    if (path_probe_inflight_) {
        LOG_WARN("PathManager: probe already in progress");
        return MigrationResult::kFailedProbeInProgress;
    }

    // 2. Pre-install the new DCID per RFC 9000 §9.5: the first packet on the new
    //    path must already carry a fresh connection ID (the interop
    //    connectionmigration check verifies exactly this on the first packet).
    //    When a preferred_address CID was supplied we install it directly
    //    (§9.6: this exact CID — not a pooled NEW_CONNECTION_ID CID — is the one
    //    the client MUST use). Otherwise we rotate to the next pooled CID.
    bool dcid_pre_rotated = false;
    if (has_preferred_cid_) {
        cid_coordinator_.SetRemoteConnectionID(preferred_cid_bytes_, preferred_cid_len_);
        dcid_pre_rotated = true;
        LOG_DEBUG("PathManager: using preferred-address CID as DCID for migration");
    } else {
        dcid_pre_rotated = cid_coordinator_.RotateRemoteConnectionID();
        if (!dcid_pre_rotated) {
            LOG_WARN("PathManager: no available remote CID for migration, aborting");
            return MigrationResult::kFailedNoAvailableCID;
        }
        LOG_DEBUG("PathManager: pre-rotated DCID for preferred-address migration");
    }

    // 3. Create a socket matching the TARGET's family. The current peer may be
    //    IPv6 while the preferred address is IPv4 (the classic interop
    //    scenario), so the family comes from the new address, not peer_addr_.
    bool target_is_ipv4 = (peer_addr.GetIp().find(':') == std::string::npos);
    ::quicx::common::Address bind_any(target_is_ipv4 ? "0.0.0.0" : "::", 0);
    common::SocketHandle new_socket = CreateBoundSocket(bind_any, target_is_ipv4);
    if (new_socket.fd < 0) {
        LOG_ERROR("PathManager: failed to create socket for preferred-address migration");
        return MigrationResult::kFailedSocketCreation;
    }

    // 4. Save old local address for reporting
    if (get_socket_cb_) {
        int32_t old_sock = get_socket_cb_();
        if (old_sock > 0) {
            common::ParseLocalAddress(old_sock, old_local_addr_);
        }
    }

    // 5. Store migration state; the CANDIDATE is the new peer address. Packets
    //    built while the probe is in flight are sent there (GetSendAddress()
    //    prefers the candidate), on the new probe socket.
    is_client_initiated_migration_ = true;
    migration_start_time_ = common::UTCTimeMsec();
    candidate_peer_addr_ = peer_addr;

    // Pre-set path_probe_inflight_ BEFORE handing the probe socket to the
    // emitter.  Without this, GetSendAddress() still returns peer_addr_ (the
    // old address) for any packets the emitter flushes on the new socket
    // between the socket handoff and StartPathValidationProbeInternal().
    // Those packets would create a new path (new_src_port, old_dst_port)
    // carrying the same DCID as the migration probe — which the interop
    // CM test rejects (RFC 9000 §9.5 expects a new DCID per path).
    path_probe_inflight_ = true;

    // 6. Hand the probe socket to its owner (MigrationController): emitter
    //    prefers it for egress, receiver registers it so the PATH_RESPONSE can
    //    arrive on it.
    if (probe_socket_ready_cb_) {
        probe_socket_ready_cb_(new_socket);
    }

    LOG_INFO("PathManager: preferred-address migration initiated, local: %s:%d -> %s:%d, peer: %s:%d -> %s:%d",
        old_local_addr_.GetIp().c_str(), old_local_addr_.GetPort(), new_local_addr_.GetIp().c_str(),
        new_local_addr_.GetPort(), peer_addr_.GetIp().c_str(), peer_addr_.GetPort(), peer_addr.GetIp().c_str(),
        peer_addr.GetPort());

    // 7. Start path validation with the pre-installed DCID.
    //    probe_state_pre_set=true because we already set path_probe_inflight_
    //    and candidate_peer_addr_ above.
    StartPathValidationProbeInternal(dcid_pre_rotated, true);

    return MigrationResult::kSuccess;
}

void PathManager::OnLocalSocketAddressChanged() {
    // RFC 9000 §9 defines a path by its local AND remote addresses. A
    // datagram arriving on a different local listener (e.g. the
    // preferred_address socket) opens a NEW path even when the peer's
    // source address is unchanged — OnObservedPeerAddress() never fires for
    // it, and the first packet we send on the new path would carry no
    // PATH_CHALLENGE, which is exactly what the interop "connectionmigration"
    // check rejects. Start validation of the (unchanged) peer on the new
    // path; the probe is emitted synchronously via send_probe_now_cb_ on
    // the already-switched active socket.
    if (path_probe_inflight_) {
        // The datagram that changed the local socket may also have changed
        // the peer address; the probe started by OnObservedPeerAddress()
        // (which ran first) already covers the new path.
        return;
    }

    LOG_INFO("PathManager: local socket changed, validating current peer %s:%d on the new path",
        peer_addr_.GetIp().c_str(), peer_addr_.GetPort());

    candidate_peer_addr_ = peer_addr_;
    StartPathValidationProbe();

    if (!path_probe_inflight_) {
        // Keys not ready — should not happen for a post-handshake migration,
        // but don't leave a dangling candidate behind.
        candidate_peer_addr_ = common::Address();
        LOG_DEBUG("PathManager: local-socket path probe not started");
    }
}

// ==================== Anti-Amplification ====================

void PathManager::EnterAntiAmplification() {
    // Disable streams while path is unvalidated to limit to probing/ACK frames
    send_manager_.SetStreamsAllowed(false);
}

void PathManager::ExitAntiAmplification() {
    // Lifts both halves of the restriction: stream scheduling and the RFC 9000
    // §8.1 byte budget. Previously only streams were re-enabled, leaving the
    // controller stuck in its unvalidated state.
    send_manager_.MarkAddressValidated();
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
    auto loop = event_loop_.lock();
    if (!loop) return;
    path_probe_task_.Cancel();
    if (!path_probe_inflight_) {
        return;
    }

    if (probe_retry_count_ >= kMaxProbeRetries) {
        // Give up probing after max retries; revert to old path
        LOG_WARN("PathManager: path validation failed after %d attempts, reverting to old path. candidate: %s:%d",
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

    path_probe_task_ = loop->AddTimer(
        life_token_,
        [this]() {
            if (!path_probe_inflight_) {
                return;
            }
            // Re-send the SAME challenge token on every retry. Regenerating it
            // here desynchronizes the wire from pending_path_challenge_data_
            // whenever a datagram is dropped by the anti-amplification gate
            // (the frame is enqueued but never transmitted): the peer then
            // responds with an older token and OnPathResponse ignores it
            // forever, so validation can never complete (observed as "path
            // validation failed after 5 attempts" on every NAT rebind).
            auto challenge = std::make_shared<PathChallengeFrame>();
            memcpy(challenge->GetData(), pending_path_challenge_data_, 8);
            LOG_DEBUG("PathManager: retrying path validation (attempt %d/%d) to %s:%d", probe_retry_count_ + 1,
                kMaxProbeRetries, candidate_peer_addr_.GetIp().c_str(), candidate_peer_addr_.GetPort());
            to_send_frame_cb_(challenge);
            ScheduleProbeRetry();
        },
        probe_retry_delay_ms_);
}

void PathManager::CompleteMigration() {
    LOG_INFO("PathManager::CompleteMigration: migration successful, switching to new socket");

    // Build migration info for callback
    MigrationInfo info;
    info.old_local_ip_ = old_local_addr_.GetIp();
    info.old_local_port_ = old_local_addr_.GetPort();
    info.new_local_ip_ = new_local_addr_.GetIp();
    info.new_local_port_ = new_local_addr_.GetPort();
    info.old_peer_ip_ = last_old_peer_addr_.GetIp();
    info.old_peer_port_ = last_old_peer_addr_.GetPort();
    info.new_peer_ip_ = peer_addr_.GetIp();
    info.new_peer_port_ = peer_addr_.GetPort();
    info.migration_start_time_ = migration_start_time_;
    info.migration_end_time_ = common::UTCTimeMsec();
    info.result_ = MigrationResult::kSuccess;
    info.is_nat_rebinding_ = false;

    // Notify MigrationController, which owns the probe fd and performs the
    // switch (promote probe -> primary, then unregister + close the retired
    // one). PathManager holds no socket state, so there is nothing to clear
    // here and no ambiguity about who closes what — the previous version had
    // both sides deferring to the other, so the old fd leaked.
    if (migration_complete_cb_) {
        migration_complete_cb_(info);
    }

    LOG_INFO("PathManager: migration completed successfully in %lu ms",
        info.migration_end_time_ - info.migration_start_time_);
}

void PathManager::HandleMigrationFailure(MigrationResult result) {
    LOG_WARN("PathManager::HandleMigrationFailure: migration failed with result %d", static_cast<int>(result));

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
    // Cancel timers.
    //
    // This runs both from the owning worker's EventLoop (e.g. during
    // HandleMigrationFailure) AND from ~PathManager, which executes on whatever
    // thread releases the last shared_ptr to the ServerConnection -- in
    // long-running perf scenarios the http3 ServerConnection map is cleared from
    // a foreign thread. Timer::Cancel() is defined to be safe from any thread, so
    // the previous IsInLoopThread()/RunInLoop-remove-by-id split (which also lost
    // the id whenever the timer had been re-registered) is gone.
    migration_timeout_task_.Cancel();
    path_probe_task_.Cancel();

    // Probe-socket disposal is MigrationController's job (it owns the fd); we
    // never held it, so there is nothing to close here. This is what fixes the
    // old double-close: this function used to close the same fd that
    // BaseConnection::OnMigrationComplete then closed again.

    // Reset addresses
    old_local_addr_ = ::quicx::common::Address();
    new_local_addr_ = ::quicx::common::Address();
    migration_start_time_ = 0;
}

common::SocketHandle PathManager::CreateBoundSocket(
    const ::quicx::common::Address& local_addr, std::optional<bool> force_ipv4) {
    // Determine socket family: an explicit override wins (preferred-address
    // migration can cross families, e.g. current peer IPv6, target IPv4);
    // otherwise match the current peer.
    bool peer_is_ipv4 = force_ipv4.has_value() ? *force_ipv4 : (peer_addr_.GetIp().find(':') == std::string::npos);

    // Create the right kind of UDP socket. UdpSocket*() returns the actual
    // address family of the resulting fd, which we forward to Bind() and hand
    // to the socket's owner (via the returned SocketHandle) so that no probing
    // (getsockname / SO_DOMAIN / IPV6_V6ONLY) is ever needed downstream.
    common::UdpSocketResult sock_ret;
    if (peer_is_ipv4) {
        // IPv4-only socket: avoids IPv6 dual-stack routing issues
        // (e.g., in Docker bridge networks that are IPv4-only).
        sock_ret = common::UdpSocket4();
        if (sock_ret.error_code_ != 0) {
            LOG_ERROR("PathManager: failed to create IPv4 UDP socket: errno=%d", sock_ret.error_code_);
            return common::SocketHandle(-1, 0);
        }
    } else {
        // IPv6 dual-stack socket for IPv6 peers.
        sock_ret = common::UdpSocket();
        if (sock_ret.error_code_ != 0) {
            LOG_ERROR("PathManager: failed to create UDP socket: errno=%d", sock_ret.error_code_);
            return common::SocketHandle(-1, 0);
        }
    }
    const int32_t sockfd = sock_ret.return_value_;
    const int32_t sock_family = sock_ret.family_;

    // Set non-blocking
    common::SocketNoblocking(sockfd);

    // Bind to specified address (for IPv4 socket, use 0.0.0.0 if address is :: or empty)
    common::Address bind_addr = local_addr;
    if (peer_is_ipv4 && (bind_addr.GetIp() == "::" || bind_addr.GetIp().empty())) {
        bind_addr.SetIp("0.0.0.0");
        bind_addr.SetAddressType(common::AddressType::kIpv4);
    }

    auto bind_ret = common::Bind(sockfd, sock_family, bind_addr);
    if (bind_ret.error_code_ != 0) {
        LOG_ERROR("PathManager: failed to bind socket to %s:%d: errno=%d", bind_addr.GetIp().c_str(),
            bind_addr.GetPort(), bind_ret.error_code_);
        common::Close(sockfd);
        return common::SocketHandle(-1, 0);
    }

    // Get the actual assigned port and address
    if (!common::ParseLocalAddress(sockfd, new_local_addr_)) {
        new_local_addr_ = bind_addr;
    }

    LOG_INFO("PathManager: created migration socket %d bound to %s:%d (peer_is_ipv4=%d)", sockfd,
        new_local_addr_.GetIp().c_str(), new_local_addr_.GetPort(), peer_is_ipv4);

    return common::SocketHandle(sockfd, sock_family);
}

}  // namespace quic
}  // namespace quicx
