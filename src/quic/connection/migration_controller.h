#ifndef QUIC_CONNECTION_MIGRATION_CONTROLLER
#define QUIC_CONNECTION_MIGRATION_CONTROLLER

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <quicx/common/if_event_loop.h>
#include <quicx/quic/type.h>

#include "common/network/address.h"
#include "quic/connection/connection_path_manager.h"
#include "quic/connection/connection_state_machine.h"
#include "quic/connection/datagram_emitter.h"
#include "quic/connection/transport_param.h"

namespace quicx {
namespace quic {

/**
 * @brief Sole owner of local socket lifecycle across a connection migration.
 *
 * RFC 9000 §9 migration used to have its socket state spread across five
 * knowers: IConnection::sockfd_, IConnection::migration_sockfd_, three
 * duplicated `(migration_sockfd_ > 0) ? ... : ...` ternaries, plus
 * PathManager::migration_socket_ and its own GetSendSocket() ternary. The two
 * copies were kept in sync by a callback pair — and PathManager's copy was
 * dead: GetSendSocket() had no callers, so the synchronisation machinery
 * outlived the state it synchronised.
 *
 * That ownership vacuum produced two real defects, both fixed here:
 *
 *   A. **Old socket leaked on success.** BaseConnection's comment said "the
 *      caller (Worker) should manage socket lifecycle"; PathManager's said
 *      "the caller (BaseConnection) is responsible for closing the old
 *      socket". Each deferred to the other, so nobody closed it — and since
 *      there was no unregister callback, it also stayed in the receiver's
 *      epoll set forever.
 *
 *   B. **Probe socket double-closed on failure.** PathManager's
 *      CleanupMigrationState() closed migration_socket_, then the completion
 *      callback had BaseConnection close migration_sockfd_ — the same fd,
 *      twice. If a new fd reused that number in between, the second close
 *      hit an unrelated socket.
 *
 * The rule is now singular and stated in one place:
 *
 *   | when            | action                                              |
 *   |-----------------|-----------------------------------------------------|
 *   | probe created   | factory -> emitter.SetProbeSocket + register_cb      |
 *   | migration ok    | emitter.SwitchToProbeSocket -> unregister + close old|
 *   | migration fails | emitter.ClearProbeSocket -> unregister + close probe  |
 *
 * This class is the only place in the connection layer allowed to call
 * close(2). DatagramEmitter deliberately hands retired fds back rather than
 * closing them itself so that this invariant stays checkable by grep.
 */
class MigrationController {
public:
    /**
     * @brief Notifies the owner that migration finished, so it can forward the
     *        event to the application.
     *
     * Kept as a callback because building the IQuicConnection shared_ptr needs
     * shared_from_this(), which only the connection object can do.
     */
    using MigrationFinishedCallback = std::function<void(const MigrationInfo&)>;

    /**
     * @brief Publishes the new local address after a successful socket switch,
     *        so the connection can refresh its cached copy.
     */
    using LocalAddressUpdatedCallback = std::function<void(const common::Address&)>;

    MigrationController(ConnectionStateMachine& state_machine, PathManager& path_manager,
        TransportParam& transport_param, DatagramEmitter& emitter,
        std::weak_ptr<common::IEventLoop> event_loop);

    ~MigrationController() = default;

    MigrationController(const MigrationController&) = delete;
    MigrationController& operator=(const MigrationController&) = delete;

    /**
     * @brief Start a client-initiated migration to a new local address.
     *
     * @param owner      Weak handle to the owning connection (type-erased). Used
     *                   to skip the deferred task if the connection is destroyed
     *                   while the request sits in the loop's queue — without it
     *                   a queued task could run against a freed `this`.
     * @param local_ip   New local IP.
     * @param local_port New local port; 0 lets the system choose.
     *
     * @note Thread-safe. Migration state (paths, CIDs, sockets, timers) is owned
     *       by the connection's event-loop thread; if called from another thread
     *       the work is dispatched onto that loop and the call blocks until the
     *       loop reports the outcome. From inside a loop callback it runs inline.
     *       Returns kFailedTimeout if the loop never picks the request up,
     *       kFailedInvalidState if the connection has no loop.
     */
    MigrationResult InitiateMigrationTo(std::weak_ptr<void> owner, const std::string& local_ip,
        uint16_t local_port);

    /**
     * @brief Convenience wrapper: migrate to the same IP on a fresh port.
     *
     * Used by interop tests so they exercise the production code path. Resolves
     * the local address family on the loop thread (see the gate in
     * InitiateMigration()), so it never races the cached local/peer addresses.
     *
     * @param owner      Weak handle to the owning connection (see above).
     *
     * @note Thread-safe with the same dispatch semantics as InitiateMigrationTo().
     */
    bool InitiateMigration(std::weak_ptr<void> owner);

    bool IsMigrationSupported() const;
    bool IsMigrationInProgress() const;

    void SetMigrationFinishedCallback(MigrationFinishedCallback cb) { migration_finished_cb_ = std::move(cb); }

    void SetLocalAddressUpdatedCallback(LocalAddressUpdatedCallback cb) { local_addr_updated_cb_ = std::move(cb); }

    void SetRegisterSocketCallback(std::function<bool(int32_t)> cb) { register_socket_cb_ = std::move(cb); }

    /**
     * @brief Supplies the controller with the connection's local-address
     *        resolver. Called on the loop thread during InitiateMigration(), so
     *        reading the connection's cached address is race-free.
     */
    void SetLocalAddressResolver(std::function<void(std::string&, uint32_t&)> cb) {
        local_addr_resolver_ = std::move(cb);
    }

    /**
     * @brief Supplies the controller with the connection's peer-address getter,
     *        used to pick the migration address family. Runs on the loop thread.
     */
    void SetPeerAddressResolver(std::function<common::Address()> cb) { peer_addr_resolver_ = std::move(cb); }

    /**
     * @brief Remove a socket from the receiver's poll set.
     *
     * Required to retire the old socket on success and the probe socket on
     * failure; without it fds accumulate in the event loop (defect A).
     */
    void SetUnregisterSocketCallback(std::function<bool(int32_t)> cb) { unregister_socket_cb_ = std::move(cb); }

    /**
     * @brief Called by PathManager when path validation resolves.
     *
     * Public because PathManager binds it as a callback; not intended for
     * direct use.
     */
    void OnMigrationComplete(const MigrationInfo& info);

private:
    // Retire an fd: drop it from the poll set, then close it. The only close(2)
    // site in the connection layer.
    void RetireSocket(int32_t fd, const char* reason);

    // Run `task` on the connection's loop thread, blocking for its result. If
    // called from the loop thread it runs inline; otherwise the task is posted
    // and the call waits (bounded by kMigrationDispatchTimeoutMs). `owner` is a
    // type-erased weak handle to the connection: if it has gone away while the
    // task was queued the task returns the supplied failure value instead of
    // touching a freed `this`.
    bool GateBool(std::weak_ptr<void> owner, std::function<bool()> task);
    MigrationResult GateResult(std::weak_ptr<void> owner, std::function<MigrationResult()> task);

    // Resolves the local address family and delegates to InitiateMigrationToImpl().
    // Only ever runs on the loop thread.
    bool InitiateMigrationImpl();

    // Core migration logic. Only ever runs on the loop thread.
    MigrationResult InitiateMigrationToImpl(const std::string& local_ip, uint16_t local_port);

    ConnectionStateMachine& state_machine_;
    PathManager& path_manager_;
    TransportParam& transport_param_;
    DatagramEmitter& emitter_;
    std::weak_ptr<common::IEventLoop> event_loop_;

    MigrationFinishedCallback migration_finished_cb_;
    LocalAddressUpdatedCallback local_addr_updated_cb_;
    std::function<bool(int32_t)> register_socket_cb_;
    std::function<bool(int32_t)> unregister_socket_cb_;
    std::function<void(std::string&, uint32_t&)> local_addr_resolver_;
    std::function<common::Address()> peer_addr_resolver_;

    bool callbacks_installed_{false};
};

}  // namespace quic
}  // namespace quicx

#endif
