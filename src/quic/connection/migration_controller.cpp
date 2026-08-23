#include <chrono>
#include <future>

#include "common/log/log.h"
#include "common/network/io_handle.h"

#include "quic/config.h"
#include "quic/connection/migration_controller.h"

namespace quicx {
namespace quic {

MigrationController::MigrationController(ConnectionStateMachine& state_machine, PathManager& path_manager,
    TransportParam& transport_param, DatagramEmitter& emitter, std::weak_ptr<common::IEventLoop> event_loop)
    : state_machine_(state_machine),
      path_manager_(path_manager),
      transport_param_(transport_param),
      emitter_(emitter),
      event_loop_(event_loop) {}

bool MigrationController::IsMigrationSupported() const {
    // Gated by the PEER's disable_active_migration declaration (RFC 9000
    // §9.4), not by our own wire declaration.
    return !transport_param_.GetPeerDisableActiveMigration();
}

bool MigrationController::IsMigrationInProgress() const {
    return path_manager_.IsPathProbeInflight();
}

bool MigrationController::InitiateMigration(std::weak_ptr<void> owner) {
    return GateBool(owner, [this]() { return InitiateMigrationImpl(); });
}

MigrationResult MigrationController::InitiateMigrationTo(std::weak_ptr<void> owner, const std::string& local_ip,
    uint16_t local_port) {
    return GateResult(owner, [this, local_ip, local_port]() { return InitiateMigrationToImpl(local_ip, local_port); });
}

bool MigrationController::InitiateMigrationImpl() {
    // RFC 9000 §9 convenience wrapper for interop tests: keep the local IP but
    // let the system pick a fresh ephemeral port. This runs on the loop thread
    // (see GateBool), so reading the connection's cached local/peer addresses
    // through the resolvers is race-free.
    LOG_INFO("MigrationController::InitiateMigration: resolving local address for migration");

    std::string current_ip;
    uint32_t current_port = 0;
    if (local_addr_resolver_) {
        local_addr_resolver_(current_ip, current_port);
    }

    if (current_ip.empty() || current_ip == "::") {
        // Dual-stack socket reports "::" as local address. For migration we need
        // to match the address family of the peer so the new socket can talk to
        // it. IPv4 peers need an IPv4 socket.
        common::Address peer;
        if (peer_addr_resolver_) {
            peer = peer_addr_resolver_();
        }
        bool peer_is_ipv4 = (peer.GetIp().find(':') == std::string::npos);
        if (peer_is_ipv4) {
            current_ip = "0.0.0.0";
            LOG_INFO("MigrationController::InitiateMigration: peer is IPv4, using 0.0.0.0");
        } else {
            current_ip = "::";
            LOG_INFO("MigrationController::InitiateMigration: peer is IPv6, using ::");
        }
    }

    MigrationResult result = InitiateMigrationToImpl(current_ip, 0);
    const bool success = (result == MigrationResult::kSuccess);
    if (!success) {
        LOG_WARN("MigrationController::InitiateMigration: failed with result %d", static_cast<int>(result));
    }
    return success;
}

MigrationResult MigrationController::InitiateMigrationToImpl(const std::string& local_ip, uint16_t local_port) {
    // RFC 9000 §9: client-initiated connection migration with local address change.
    LOG_INFO("MigrationController::InitiateMigrationTo: starting migration to %s:%d", local_ip.c_str(), local_port);

    if (!state_machine_.CanSendData()) {
        LOG_WARN("MigrationController::InitiateMigrationTo: connection not in connected state");
        return MigrationResult::kFailedInvalidState;
    }

    EnsureCallbacksInstalled();

    common::Address local_addr(local_ip, local_port);
    return path_manager_.InitiateMigrationToAddress(local_addr);
}

MigrationResult MigrationController::InitiateMigrationToPeer(std::weak_ptr<void> owner, const std::string& peer_ip,
    uint16_t peer_port) {
    return GateResult(owner, [this, peer_ip, peer_port]() { return InitiateMigrationToPeerImpl(peer_ip, peer_port); });
}

MigrationResult MigrationController::InitiateMigrationToPeerImpl(const std::string& peer_ip, uint16_t peer_port) {
    // RFC 9000 §9.6: migrate to the server's advertised preferred address.
    LOG_INFO("MigrationController::InitiateMigrationToPeer: migrating to peer %s:%d", peer_ip.c_str(), peer_port);

    if (!state_machine_.CanSendData()) {
        LOG_WARN("MigrationController::InitiateMigrationToPeer: connection not in connected state");
        return MigrationResult::kFailedInvalidState;
    }

    EnsureCallbacksInstalled();

    // RFC 9000 §9.6: the new DCID on the migrated path is the connection ID
    // carried inside the server's preferred_address transport parameter. Hand it
    // to PathManager so it installs that exact CID instead of rotating to a
    // pooled NEW_CONNECTION_ID CID (which a server need not provide).
    const std::string& pref_cid = transport_param_.GetPreferredAddressCID();
    if (!pref_cid.empty()) {
        path_manager_.SetPreferredConnectionID(reinterpret_cast<const uint8_t*>(pref_cid.data()),
            static_cast<uint16_t>(pref_cid.size()));
    }

    common::Address peer_addr(peer_ip, peer_port);
    return path_manager_.InitiateMigrationToPeerAddress(peer_addr);
}

void MigrationController::EnsureCallbacksInstalled() {
    // Install callbacks once. PathManager creates the probe socket but does not
    // keep it: it hands the fd straight here, so there is exactly one owner.
    if (callbacks_installed_) {
        return;
    }
    path_manager_.SetSocketFactoryCallbacks([this]() { return emitter_.GetActiveSocket(); },
        [this](int32_t probe_fd) {
            emitter_.SetProbeSocket(probe_fd);

            // Register immediately: PATH_RESPONSE arrives on the probe
            // socket, so it must be in the poll set before validation
            // starts. (The old code registered it after
            // InitiateMigrationToAddress returned, which left a window.)
            if (register_socket_cb_ && probe_fd > 0) {
                if (!register_socket_cb_(probe_fd)) {
                    LOG_ERROR("MigrationController: failed to register probe socket %d with receiver", probe_fd);
                } else {
                    LOG_INFO("MigrationController: registered probe socket %d with receiver", probe_fd);
                }
            }
        });

    path_manager_.SetMigrationCompleteCallback([this](const MigrationInfo& info) { OnMigrationComplete(info); });
    callbacks_installed_ = true;
}

bool MigrationController::GateBool(std::weak_ptr<void> owner, std::function<bool()> task) {
    auto loop = event_loop_.lock();
    if (!loop) {
        LOG_WARN("MigrationController::InitiateMigration: no event loop, refusing to migrate");
        return false;
    }

    if (loop->IsInLoopThread()) {
        return task();
    }

    auto done = std::make_shared<std::promise<bool>>();
    auto result = done->get_future();
    // `owner` is a type-erased weak handle to the owning connection. If it has
    // gone away while the task was queued, running `task()` would dereference a
    // freed `this`, so we skip it. The loop itself stays alive for the duration
    // of the posted task because we captured `loop` below.
    std::shared_ptr<common::IEventLoop> keep_loop_alive = loop;
    loop->PostTask([this, owner, done, task, keep_loop_alive]() {
        if (!owner.lock()) {
            done->set_value(false);
            return;
        }
        done->set_value(task());
    });

    if (result.wait_for(std::chrono::milliseconds(kMigrationDispatchTimeoutMs)) != std::future_status::ready) {
        LOG_ERROR("MigrationController::InitiateMigration: event loop did not run the migration task within %ums",
            kMigrationDispatchTimeoutMs);
        return false;
    }
    return result.get();
}

MigrationResult MigrationController::GateResult(std::weak_ptr<void> owner, std::function<MigrationResult()> task) {
    auto loop = event_loop_.lock();
    if (!loop) {
        LOG_WARN("MigrationController::InitiateMigrationTo: no event loop, refusing to migrate");
        return MigrationResult::kFailedInvalidState;
    }

    if (loop->IsInLoopThread()) {
        return task();
    }

    auto done = std::make_shared<std::promise<MigrationResult>>();
    auto result = done->get_future();
    std::shared_ptr<common::IEventLoop> keep_loop_alive = loop;
    loop->PostTask([this, owner, done, task, keep_loop_alive]() {
        if (!owner.lock()) {
            done->set_value(MigrationResult::kFailedInvalidState);
            return;
        }
        done->set_value(task());
    });

    if (result.wait_for(std::chrono::milliseconds(kMigrationDispatchTimeoutMs)) != std::future_status::ready) {
        LOG_ERROR("MigrationController::InitiateMigrationTo: event loop did not run the migration task within %ums",
            kMigrationDispatchTimeoutMs);
        return MigrationResult::kFailedTimeout;
    }
    return result.get();
}

void MigrationController::OnMigrationComplete(const MigrationInfo& info) {
    LOG_INFO("MigrationController::OnMigrationComplete: result=%d, is_nat_rebinding=%d",
        static_cast<int>(info.result_), info.is_nat_rebinding_);

    if (info.result_ == MigrationResult::kSuccess) {
        if (emitter_.GetProbeSocket() > 0) {
            // Promote the probe socket; the emitter hands back the fd it
            // retired so we can dispose of it. Nobody used to do this, which
            // is how the old socket leaked (both sides of the old code assumed
            // the other side would close it).
            const int32_t retired = emitter_.SwitchToProbeSocket();

            // Refresh the cached local address from the new socket.
            common::Address new_local;
            if (local_addr_updated_cb_ && common::ParseLocalAddress(emitter_.GetActiveSocket(), new_local)) {
                local_addr_updated_cb_(new_local);
            }

            LOG_INFO("MigrationController: switched to probe socket %d (retired: %d)", emitter_.GetActiveSocket(),
                retired);

            RetireSocket(retired, "migration succeeded");
        }
    } else {
        // Failure: the probe socket is ours to dispose of. PathManager no
        // longer closes it (it never holds it), so there is exactly one close.
        const int32_t probe = emitter_.GetProbeSocket();
        if (probe > 0) {
            emitter_.ClearProbeSocket();
            RetireSocket(probe, "migration failed");
        }
    }

    if (migration_finished_cb_) {
        migration_finished_cb_(info);
    }
}

void MigrationController::RetireSocket(int32_t fd, const char* reason) {
    if (fd <= 0) {
        return;
    }

    // Order matters: leave the poll set before closing, otherwise the event
    // loop can hand out an event for a closed fd.
    if (unregister_socket_cb_) {
        if (!unregister_socket_cb_(fd)) {
            LOG_WARN("MigrationController: failed to unregister socket %d (%s)", fd, reason);
        }
    }

    common::Close(fd);
}

}  // namespace quic
}  // namespace quicx
