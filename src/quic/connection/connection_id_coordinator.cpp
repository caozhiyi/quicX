#include <cstdlib>
#include <cstring>
#include <sstream>

#include <openssl/mem.h>  // CRYPTO_memcmp

#include "common/log/log.h"
#include "common/qlog/qlog.h"

#include "quic/connection/connection_id_coordinator.h"
#include "quic/connection/stateless_reset_token_generator.h"
#include "quic/connection/controler/send_manager.h"
#include "quic/frame/new_connection_id_frame.h"
#include "quic/frame/retire_connection_id_frame.h"

namespace quicx {
namespace quic {

namespace {
// Helper to convert ConnectionID to hex string for qlog
std::string CIDToHexString(const ConnectionID& cid) {
    std::ostringstream oss;
    const uint8_t* id = cid.GetID();
    uint8_t len = cid.GetLength();
    for (uint8_t i = 0; i < len; ++i) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", id[i]);
        oss << buf;
    }
    return oss.str();
}
}  // anonymous namespace

void ConnectionIDCoordinator::AddPeerStatelessResetToken(const uint8_t* token) {
    if (token == nullptr) {
        return;
    }

    std::array<uint8_t, 16> entry{};
    memcpy(entry.data(), token, entry.size());

    // Ignore duplicates so repeated NEW_CONNECTION_ID frames carrying the same
    // token cannot fill the table.
    for (const auto& existing : peer_reset_tokens_) {
        if (existing == entry) {
            return;
        }
    }

    if (peer_reset_tokens_.size() >= kMaxPeerResetTokens) {
        // Drop the oldest: the peer has moved on to newer CIDs, and an unbounded
        // table is a memory-growth primitive for a hostile peer.
        peer_reset_tokens_.erase(peer_reset_tokens_.begin());
    }
    peer_reset_tokens_.push_back(entry);
}

bool ConnectionIDCoordinator::IsPeerStatelessResetToken(const uint8_t* token) const {
    if (token == nullptr) {
        return false;
    }

    // Accumulate over every entry rather than returning early, so the time taken
    // does not reveal which entry matched or how far the scan got.
    unsigned matched = 0;
    for (const auto& existing : peer_reset_tokens_) {
        matched |= (CRYPTO_memcmp(existing.data(), token, existing.size()) == 0) ? 1u : 0u;
    }
    return matched != 0;
}

ConnectionIDCoordinator::ConnectionIDCoordinator(std::shared_ptr<common::IEventLoop> event_loop,
    SendManager& send_manager, AddConnectionIDCallback add_cb, RetireConnectionIDCallback retire_cb):
    event_loop_(event_loop),
    send_manager_(send_manager),
    add_conn_id_cb_(add_cb),
    retire_conn_id_cb_(retire_cb) {}

// ==================== Initialization ====================

void ConnectionIDCoordinator::Initialize() {
    // Remote CID manager: manages CIDs provided by peer for us to use
    // We manually send RETIRE_CONNECTION_ID when switching CIDs, not automatically on retire
    remote_conn_id_manager_ = std::make_shared<ConnectionIDManager>();

    // Local CID manager: manages CIDs we provide to peer
    // Automatically calls add_cb/retire_cb when CIDs are added/retired
    local_conn_id_manager_ =
        std::make_shared<ConnectionIDManager>([this](ConnectionID& id) { this->AddConnectionId(id); },
            [this](ConnectionID& id) { this->RetireConnectionId(id); });

    // Set connection ID managers in send manager
    send_manager_.SetRemoteConnectionIDManager(remote_conn_id_manager_);
    send_manager_.SetLocalConnectionIDManager(local_conn_id_manager_);
}

// ==================== Connection ID Operations ====================

void ConnectionIDCoordinator::AddConnectionId(ConnectionID& id) {
    if (add_conn_id_cb_) {
        add_conn_id_cb_(id);
    }
}

void ConnectionIDCoordinator::RetireConnectionId(ConnectionID& id) {
    if (retire_conn_id_cb_) {
        retire_conn_id_cb_(id);
    }
}

uint64_t ConnectionIDCoordinator::GetConnectionIDHash() const {
    if (!local_conn_id_manager_) {
        LOG_ERROR("ConnectionIDCoordinator::GetConnectionIDHash: local_conn_id_manager_ is null");
        return 0;
    }
    return local_conn_id_manager_->GetCurrentID().Hash();
}

std::vector<uint64_t> ConnectionIDCoordinator::GetAllLocalCIDHashes() const {
    if (!local_conn_id_manager_) {
        LOG_ERROR("ConnectionIDCoordinator::GetAllLocalCIDHashes: local_conn_id_manager_ is null");
        return std::vector<uint64_t>();
    }
    return local_conn_id_manager_->GetAllIDHashes();
}

// ==================== Connection ID Pool Management ====================

void ConnectionIDCoordinator::CheckAndReplenishLocalCIDPool() {
    if (!local_conn_id_manager_) {
        return;
    }

    size_t current_count = local_conn_id_manager_->GetAvailableIDCount();

    // Ensure we have enough *spare* CIDs beyond the one currently in use
    // For connection migration, the peer needs additional CIDs to switch to
    // current_count includes the CID currently in use, so we need total >= kMinLocalCIDPoolSize + 1
    // to ensure kMinLocalCIDPoolSize spare CIDs
    if (current_count >= kMinLocalCIDPoolSize + 1) {
        return;
    }

    // Calculate how many CIDs to generate (up to max pool size and respecting peer limit)
    size_t max_allowed = peer_active_cid_limit_;
    if (current_count >= max_allowed) {
        return;
    }
    size_t to_generate = std::min<size_t>(kMaxLocalCIDPoolSize, max_allowed - current_count);
    // Also cap by kMaxLocalCIDPoolSize - current_count to avoid over-generating beyond our own max
    to_generate = std::min<size_t>(to_generate, kMaxLocalCIDPoolSize - current_count);

    LOG_DEBUG("ConnectionIDCoordinator: replenishing local CID pool: current=%zu, generating=%zu", current_count,
        to_generate);

    for (size_t i = 0; i < to_generate; ++i) {
        // Generate new connection ID
        ConnectionID new_cid = local_conn_id_manager_->Generator();

        // Create and send NEW_CONNECTION_ID frame
        auto frame = std::make_shared<NewConnectionIDFrame>();
        frame->SetSequenceNumber(new_cid.GetSequenceNumber());
        frame->SetRetirePriorTo(0);  // Don't force retirement of older IDs
        frame->SetConnectionID(const_cast<uint8_t*>(new_cid.GetID()), new_cid.GetLength());

        // Stateless reset token (RFC 9000 §10.3). Derived from the CID with the
        // static key rather than drawn at random: the token we advertise here must
        // still be computable after this process has forgotten the connection
        // entirely, which is precisely when a Stateless Reset needs to be sent.
        uint8_t reset_token[kStatelessResetTokenLength];
        if (!StatelessResetTokenGenerator::Instance().Generate(new_cid.GetID(), new_cid.GetLength(), reset_token)) {
            LOG_ERROR("ConnectionIDCoordinator: reset token derivation failed, not advertising this CID");
            break;
        }
        frame->SetStatelessResetToken(reset_token);

        // Send frame through send manager
        send_manager_.EnqueueFrame(frame);

        // Log connection_id_updated event for pool replenishment
        if (qlog_trace_) {
            common::ConnectionIdUpdatedData cid_data;
            cid_data.owner = "local";
            cid_data.new_id = CIDToHexString(new_cid);
            cid_data.trigger = "pool_replenish";
            QLOG_CONNECTION_ID_UPDATED(qlog_trace_, cid_data);
        }

        LOG_DEBUG("ConnectionIDCoordinator: generated NEW_CONNECTION_ID: seq=%llu, len=%d", new_cid.GetSequenceNumber(),
            new_cid.GetLength());
    }
}

bool ConnectionIDCoordinator::RotateRemoteConnectionID() {
    if (!remote_conn_id_manager_) {
        LOG_ERROR("ConnectionIDCoordinator::RotateRemoteConnectionID: remote_conn_id_manager_ is null");
        return false;
    }

    // Get old CID before rotation
    auto old_cid = remote_conn_id_manager_->GetCurrentID();

    // Switch to next remote CID
    if (!remote_conn_id_manager_->UseNextID()) {
        LOG_WARN("ConnectionIDCoordinator::RotateRemoteConnectionID: no next CID available");
        return false;
    }

    auto new_cid = remote_conn_id_manager_->GetCurrentID();

    // Defer the RETIRE_CONNECTION_ID for the previous DCID. RFC 9000 §9.2 and
    // peer path-deletion rules require that a CID still bound to an active path
    // not be retired. We retire it only after the migration path is validated
    // (PathManager::OnPathResponse -> RetirePendingRemoteConnectionID), otherwise
    // the peer sees a RETIRE_CONNECTION_ID for a path it still considers live and
    // aborts the connection (e.g. picoquic: "Cannot delete path through which
    // packet arrives").
    pending_retire_remote_seq_ = old_cid.GetSequenceNumber();
    has_pending_retire_remote_cid_ = true;

    // Log connection_id_updated event for CID rotation
    if (qlog_trace_) {
        common::ConnectionIdUpdatedData cid_data;
        cid_data.owner = "remote";
        cid_data.old_id = CIDToHexString(old_cid);
        cid_data.new_id = CIDToHexString(new_cid);
        cid_data.trigger = "cid_rotation";
        QLOG_CONNECTION_ID_UPDATED(qlog_trace_, cid_data);
    }

    LOG_DEBUG("ConnectionIDCoordinator: rotated remote CID, deferred RETIRE for seq=%llu", old_cid.GetSequenceNumber());

    return true;
}

void ConnectionIDCoordinator::SetRemoteConnectionID(const uint8_t* id, uint16_t len) {
    if (!remote_conn_id_manager_) {
        LOG_ERROR("ConnectionIDCoordinator::SetRemoteConnectionID: remote_conn_id_manager_ is null");
        return;
    }
    if (id == nullptr || len == 0) {
        LOG_ERROR("ConnectionIDCoordinator::SetRemoteConnectionID: invalid CID");
        return;
    }
    // Preferred-address migration (RFC 9000 §9.6): the new DCID is the CID carried
    // in the server's preferred_address transport parameter. We install it directly
    // as the current remote CID (same mechanism the client uses to switch DCID from
    // ODCID to server SCID during the handshake), assigning it the next sequence
    // number so any later RETIRE is unambiguous. No CID rotation / RETIRE of the
    // previous DCID is triggered here.
    uint64_t seq = remote_conn_id_manager_->GetCurrentID().GetSequenceNumber() + 1;
    remote_conn_id_manager_->SetCurrentID(id, len, seq);
    LOG_DEBUG("ConnectionIDCoordinator: set remote CID to preferred-address CID (seq=%llu, len=%d)", seq, len);
}

void ConnectionIDCoordinator::RetirePendingRemoteConnectionID() {
    if (!has_pending_retire_remote_cid_) {
        return;
    }
    has_pending_retire_remote_cid_ = false;
    // Enqueue RETIRE_CONNECTION_ID for the previously-used remote CID now that
    // the migration path is validated and the old CID is no longer active.
    auto retire = std::make_shared<RetireConnectionIDFrame>();
    retire->SetSequenceNumber(pending_retire_remote_seq_);
    send_manager_.EnqueueFrame(retire);
    LOG_DEBUG("ConnectionIDCoordinator: flushed deferred RETIRE for seq=%llu", pending_retire_remote_seq_);
}

void ConnectionIDCoordinator::SetPeerActiveConnectionIDLimit(uint64_t limit) {
    if (limit < 2) {
        limit = 2;  // RFC 9000: MUST be at least 2
    }
    peer_active_cid_limit_ = limit;
    CheckAndReplenishLocalCIDPool();  // Trigger replenishment if limit increased
}

}  // namespace quic
}  // namespace quicx
