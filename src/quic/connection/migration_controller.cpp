#include "quic/connection/migration_controller.h"

#include "common/log/log.h"
#include "common/network/io_handle.h"

namespace quicx {
namespace quic {

MigrationController::MigrationController(ConnectionStateMachine& state_machine, PathManager& path_manager,
    TransportParam& transport_param, DatagramEmitter& emitter)
    : state_machine_(state_machine),
      path_manager_(path_manager),
      transport_param_(transport_param),
      emitter_(emitter) {}

bool MigrationController::IsMigrationSupported() const {
    return !transport_param_.GetDisableActiveMigration();
}

bool MigrationController::IsMigrationInProgress() const {
    return path_manager_.IsPathProbeInflight();
}

bool MigrationController::InitiateMigration(const std::string& current_ip) {
    // port=0 means the system picks a fresh port, which forces a real socket
    // switch — the same code path production migration takes.
    MigrationResult result = InitiateMigrationTo(current_ip, 0);

    const bool success = (result == MigrationResult::kSuccess);
    if (!success) {
        LOG_WARN("MigrationController::InitiateMigration: failed with result %d", static_cast<int>(result));
    }
    return success;
}

MigrationResult MigrationController::InitiateMigrationTo(const std::string& local_ip, uint16_t local_port) {
    // RFC 9000 §9: client-initiated connection migration with local address change.
    LOG_INFO("MigrationController::InitiateMigrationTo: starting migration to %s:%d", local_ip.c_str(), local_port);

    if (!state_machine_.CanSendData()) {
        LOG_WARN("MigrationController::InitiateMigrationTo: connection not in connected state");
        return MigrationResult::kFailedInvalidState;
    }

    // Install callbacks once. PathManager creates the probe socket but does not
    // keep it: it hands the fd straight here, so there is exactly one owner.
    if (!callbacks_installed_) {
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

    common::Address local_addr(local_ip, local_port);
    return path_manager_.InitiateMigrationToAddress(local_addr);
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
