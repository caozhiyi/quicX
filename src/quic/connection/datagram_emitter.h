#ifndef QUIC_CONNECTION_DATAGRAM_EMITTER
#define QUIC_CONNECTION_DATAGRAM_EMITTER

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "common/buffer/if_buffer.h"
#include "common/network/address.h"
#include "common/qlog/qlog_trace.h"
#include "quic/connection/controler/send_control.h"
#include "quic/udp/if_sender.h"
#include "quic/udp/net_packet.h"

namespace quicx {
namespace quic {

/**
 * @brief Sole owner of "how bytes get on the wire" for one QUIC connection.
 *
 * This class exists to give three previously ownerless invariants a home:
 *
 * 1. **Datagram brackets must pair.** Every outbound UDP datagram must be
 *    bracketed by SendControl::BeginSendDatagram / EndSendDatagram, *including
 *    on every failure path*, or the next datagram inherits a stale
 *    datagram_id and qlog coalescing rendering breaks. This used to be hand-
 *    paired across 8 call sites in BaseConnection (3 success + 5 bare
 *    EndSendDatagram calls on early-return paths). It is now structural: see
 *    Scope.
 *
 * 2. **One egress path.** There used to be two (SendBuffer and SendImmediate)
 *    which had drifted apart in three uncontrolled ways — only one warmed the
 *    sockaddr cache, only one set a (never-read) timestamp, and only one
 *    honoured the batch sink. They are now a single Emit() with an explicit
 *    bypass_batch flag.
 *
 * 3. **One socket knower.** The expression
 *    `(migration_sockfd_ > 0) ? migration_sockfd_ : sockfd_` was duplicated in
 *    three places across two files, and PathManager kept a fourth copy of the
 *    state plus a fifth ternary of its own. GetActiveSocket() is now the only
 *    implementation.
 *
 * Ownership note: this class never calls close(2). It hands the retired fd
 * back from SwitchToProbeSocket() and lets MigrationController — the sole
 * owner of socket lifecycle — close and unregister it. That split is what
 * fixes the "old socket leaked on successful migration" and "probe socket
 * double-closed on failure" defects.
 */
class DatagramEmitter {
public:
    /**
     * @brief Supplies the destination address for each outbound datagram.
     *
     * The provider is also responsible for warming the sockaddr cache on the
     * long-lived address object it copies from. That warm-up cannot live here:
     * it must target storage that outlives the call so the cache is amortised
     * across datagrams rather than rebuilt per send. See BaseConnection's
     * construction site for the concrete implementation.
     */
    using AddressProvider = std::function<common::Address()>;

    DatagramEmitter(SendControl& send_control, AddressProvider addr_provider,
        std::shared_ptr<common::QlogTrace> qlog_trace);

    ~DatagramEmitter() = default;

    DatagramEmitter(const DatagramEmitter&) = delete;
    DatagramEmitter& operator=(const DatagramEmitter&) = delete;

    /**
     * @brief RAII bracket around one outbound UDP datagram.
     *
     * Open() begins the datagram; Commit() ships it; the destructor closes an
     * uncommitted datagram. Because the destructor runs on every path out of
     * the enclosing scope — including early returns and exceptions — a
     * forgotten EndSendDatagram is no longer expressible.
     */
    class Scope {
    public:
        ~Scope();

        Scope(Scope&& other) noexcept;
        Scope& operator=(Scope&& other) noexcept;
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

        /**
         * @brief Ship the datagram: close the qlog bracket and send the bytes.
         *
         * @param buffer      Datagram payload (one or more coalesced packets).
         * @param bypass_batch When true, send directly via ISender even if a
         *                     batch sink is installed. This is the old
         *                     SendImmediate semantic, used for handshake and
         *                     immediate-ACK datagrams that must not wait for
         *                     the end-of-round sendmmsg flush.
         * @return true if the bytes were handed off successfully.
         */
        bool Commit(std::shared_ptr<common::IBuffer> buffer, bool bypass_batch = false);

    private:
        friend class DatagramEmitter;
        explicit Scope(DatagramEmitter* owner): owner_(owner) {}

        DatagramEmitter* owner_{nullptr};
        bool committed_{false};
    };

    /**
     * @brief Begin a datagram. Must be held in a local; do not discard.
     */
    [[nodiscard]] Scope Open();

    void SetSender(std::shared_ptr<ISender> sender) { sender_ = std::move(sender); }

    /**
     * @brief Install the qlog trace. Traces are created after the connection is
     *        constructed (once the ODCID is known), hence the setter.
     */
    void SetQlogTrace(std::shared_ptr<common::QlogTrace> qlog_trace) { qlog_trace_ = std::move(qlog_trace); }

    /**
     * @brief Install the per-drain-round batch sink (Worker's sendmmsg batch).
     *
     * Passing nullptr reverts to direct sends. The emitter does not own the
     * vector; Worker flushes and clears it before returning from ProcessSend.
     */
    void SetBatchSink(std::vector<std::shared_ptr<NetPacket>>* sink) { send_sink_ = sink; }

    // ==================== socket ownership ====================

    // Called from the worker on every received datagram to keep the egress fd
    // in sync with the local socket the packet arrived on. During an in-progress
    // migration the probe socket is the preferred egress fd (GetActiveSocket()
    // prefers probe_sockfd_ while non-zero), so a PATH_RESPONSE that arrives on
    // the probe socket must NOT clobber the primary sockfd_: doing so makes
    // SwitchToProbeSocket() retire the *new* socket instead of the *old* one,
    // leaving every subsequent send to fail with EBADF.
    void SetSocket(int32_t fd) {
        if (probe_sockfd_ > 0 && fd == probe_sockfd_) {
            return;
        }
        sockfd_ = fd;
    }

    /**
     * @brief Install the migration probe socket (preferred while non-zero).
     */
    void SetProbeSocket(int32_t fd) { probe_sockfd_ = fd; }

    int32_t GetProbeSocket() const { return probe_sockfd_; }

    /**
     * @brief Promote the probe socket to primary (migration succeeded).
     *
     * @return The retired primary fd. The caller owns it and must unregister
     *         and close it — this class never closes sockets.
     */
    [[nodiscard]] int32_t SwitchToProbeSocket();

    /**
     * @brief Abandon the probe socket (migration failed).
     *
     * The caller is responsible for unregistering and closing the fd it
     * obtained from GetProbeSocket() beforehand.
     */
    void ClearProbeSocket() { probe_sockfd_ = 0; }

    /**
     * @brief The fd outbound datagrams go out on. Sole implementation of the
     *        "prefer probe socket while migrating" rule.
     */
    int32_t GetActiveSocket() const { return (probe_sockfd_ > 0) ? probe_sockfd_ : sockfd_; }

    // ==================== test seams ====================

    /**
     * @brief Datagram id that Open() will assign next (1-based; 0 means none).
     */
    uint64_t GetNextDatagramIdForTest() const { return next_datagram_id_; }

private:
    // Called by Scope::Commit.
    bool Emit(std::shared_ptr<common::IBuffer> buffer, bool bypass_batch);
    // Called by ~Scope when the datagram was never committed.
    void AbortDatagram();
    // Shared tail of both paths: close the bracket, emit qlog if non-empty.
    void CloseBracketAndLog();

    SendControl& send_control_;
    AddressProvider addr_provider_;
    std::shared_ptr<common::QlogTrace> qlog_trace_;

    std::shared_ptr<ISender> sender_;
    std::vector<std::shared_ptr<NetPacket>>* send_sink_{nullptr};

    int32_t sockfd_{0};
    int32_t probe_sockfd_{0};

    // The id 0 is reserved for "no datagram open"; first real id is 1.
    uint64_t next_datagram_id_{1};
};

}  // namespace quic
}  // namespace quicx

#endif
