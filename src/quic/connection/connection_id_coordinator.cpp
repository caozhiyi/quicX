#include <cstdlib>
#include <cstring>
#include <sstream>

#include "common/log/log.h"
#include "common/qlog/qlog.h"

#include "quic/connection/connection_id_coordinator.h"
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
        common::LOG_ERROR("ConnectionIDCoordinator::GetConnectionIDHash: local_conn_id_manager_ is null");
        return 0;
    }
    return local_conn_id_manager_->GetCurrentID().Hash();
}

std::vector<uint64_t> ConnectionIDCoordinator::GetAllLocalCIDHashes() const {
    if (!local_conn_id_manager_) {
        common::LOG_ERROR("ConnectionIDCoordinator::GetAllLocalCIDHashes: local_conn_id_manager_ is null");
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

    common::LOG_DEBUG("ConnectionIDCoordinator: replenishing local CID pool: current=%zu, generating=%zu",
        current_count, to_generate);

    for (size_t i = 0; i < to_generate; ++i) {
        // Generate new connection ID
        ConnectionID new_cid = local_conn_id_manager_->Generator();

        // Create and send NEW_CONNECTION_ID frame
        auto frame = std::make_shared<NewConnectionIDFrame>();
        frame->SetSequenceNumber(new_cid.GetSequenceNumber());
        frame->SetRetirePriorTo(0);  // Don't force retirement of older IDs
        frame->SetConnectionID(const_cast<uint8_t*>(new_cid.GetID()), new_cid.GetLength());

        // Generate stateless reset token (using random data for now)
        // In production, this should be derived from a secret
        uint8_t reset_token[16];
        for (int j = 0; j < 16; ++j) {
            reset_token[j] = static_cast<uint8_t>(rand() % 256);
        }
        frame->SetStatelessResetToken(reset_token);

        // Send frame through send manager
        send_manager_.ToSendFrame(frame);

        // Log connection_id_updated event for pool replenishment
        if (qlog_trace_) {
            common::ConnectionIdUpdatedData cid_data;
            cid_data.owner = "local";
            cid_data.new_id = CIDToHexString(new_cid);
            cid_data.trigger = "pool_replenish";
            QLOG_CONNECTION_ID_UPDATED(qlog_trace_, cid_data);
        }

        common::LOG_DEBUG("ConnectionIDCoordinator: generated NEW_CONNECTION_ID: seq=%llu, len=%d",
            new_cid.GetSequenceNumber(), new_cid.GetLength());
    }
}

bool ConnectionIDCoordinator::RotateRemoteConnectionID() {
    if (!remote_conn_id_manager_) {
        common::LOG_ERROR("ConnectionIDCoordinator::RotateRemoteConnectionID: remote_conn_id_manager_ is null");
        return false;
    }

    // Get old CID before rotation
    auto old_cid = remote_conn_id_manager_->GetCurrentID();

    // Switch to next remote CID
    if (!remote_conn_id_manager_->UseNextID()) {
        common::LOG_WARN("ConnectionIDCoordinator::RotateRemoteConnectionID: no next CID available");
        return false;
    }

    auto new_cid = remote_conn_id_manager_->GetCurrentID();

    // Send RETIRE_CONNECTION_ID for the old CID
    auto retire = std::make_shared<RetireConnectionIDFrame>();
    retire->SetSequenceNumber(old_cid.GetSequenceNumber());
    send_manager_.ToSendFrame(retire);

    // Log connection_id_updated event for CID rotation
    if (qlog_trace_) {
        common::ConnectionIdUpdatedData cid_data;
        cid_data.owner = "remote";
        cid_data.old_id = CIDToHexString(old_cid);
        cid_data.new_id = CIDToHexString(new_cid);
        cid_data.trigger = "cid_rotation";
        QLOG_CONNECTION_ID_UPDATED(qlog_trace_, cid_data);
    }

    common::LOG_DEBUG("ConnectionIDCoordinator: rotated remote CID, retired seq=%llu", old_cid.GetSequenceNumber());

    return true;
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
