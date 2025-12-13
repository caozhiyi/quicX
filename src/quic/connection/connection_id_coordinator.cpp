#include <cstdlib>
#include <cstring>

#include "common/log/log.h"

#include "quic/connection/connection_id_coordinator.h"
#include "quic/connection/controler/send_manager.h"
#include "quic/frame/new_connection_id_frame.h"
#include "quic/frame/retire_connection_id_frame.h"

namespace quicx {
namespace quic {

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

    // Calculate how many CIDs to generate (up to max pool size)
    size_t to_generate = std::min<size_t>(kMaxLocalCIDPoolSize - current_count, kMaxLocalCIDPoolSize);

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

    // Send RETIRE_CONNECTION_ID for the old CID
    auto retire = std::make_shared<RetireConnectionIDFrame>();
    retire->SetSequenceNumber(old_cid.GetSequenceNumber());
    send_manager_.ToSendFrame(retire);

    common::LOG_DEBUG("ConnectionIDCoordinator: rotated remote CID, retired seq=%llu", old_cid.GetSequenceNumber());

    return true;
}

}  // namespace quic
}  // namespace quicx
