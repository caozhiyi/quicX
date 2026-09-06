#ifndef QUIC_CONNECTION_DATAGRAM_EMITTER
#define QUIC_CONNECTION_DATAGRAM_EMITTER

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "common/buffer/if_buffer.h"
#include "common/network/address.h"
#include "common/network/socket_handle.h"
#include "common/qlog/qlog_trace.h"

#include "quic/connection/controller/send_control.h"
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

    /**
     * @brief Anti-amplification gate consulted before every outbound datagram.
     *
     * RFC 9000 §8.1 requires a server to send at most three times as many bytes
     * as it has received from an unvalidated address. Because this class is the
     * sole egress point (see invariant 2 above), enforcing the limit here is what
     * makes the limit unavoidable rather than advisory: there is no other way for
     * bytes to reach the wire.
     *
     * Receives the full UDP datagram length. Returns false to drop the datagram;
     * a false return also charges nothing against the budget.
     */
    using AmpBudgetCheck = std::function<bool(uint32_t datagram_bytes)>;

    /**
     * @brief Read-only companion to AmpBudgetCheck: "would |bytes| fit?".
     *
     * AmpBudgetCheck debits (it is TryCharge), so it cannot be used to size an
     * outgoing datagram — asking twice would charge twice. This one only
     * answers the question, and must therefore never debit. It exists so the
     * §14.1 padding below can decide whether padding to the floor is even
     * affordable before the bytes are written.
     */
    using AmpBudgetQuery = std::function<bool(uint32_t datagram_bytes)>;

    DatagramEmitter(
        SendControl& send_control, AddressProvider addr_provider, std::shared_ptr<common::QlogTrace> qlog_trace);

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
        explicit Scope(DatagramEmitter* owner):
            owner_(owner) {}

        DatagramEmitter* owner_{nullptr};
        bool committed_{false};
    };

    /**
     * @brief Begin a datagram. Must be held in a local; do not discard.
     */
    [[nodiscard]] Scope Open();

    void SetSender(std::shared_ptr<ISender> sender) { sender_ = std::move(sender); }

    /**
     * @brief Install the RFC 9000 §8.1 budget gate. Absent gate == no limit.
     */
    void SetAmpBudgetCheck(AmpBudgetCheck cb) { amp_budget_check_ = std::move(cb); }

    /**
     * @brief Install the read-only budget probe used to size the §14.1 padding.
     * Absent probe == assume the budget allows it.
     */
    void SetAmpBudgetQuery(AmpBudgetQuery cb) { amp_budget_query_ = std::move(cb); }

    /**
     * @brief Mark the datagram being built as carrying an Initial packet.
     *
     * Such a datagram must reach the RFC 9000 §14.1 floor of 1200 B before it
     * is shipped. The padding is applied in Emit(), against the datagram's
     * measured size, rather than being predicted while the packet is built.
     * Cleared by Open() and after every Emit().
     */
    void MarkDatagramCarriesInitial() { datagram_carries_initial_ = true; }

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

    // Called from the worker on every received datagram to keep the egress
    // socket in sync with the local socket the packet arrived on. During an
    // in-progress migration the probe socket is the preferred egress socket
    // (GetActiveSocket() prefers probe_sock_ while non-zero), so a
    // PATH_RESPONSE that arrives on the probe socket must NOT clobber the
    // primary sock_: doing so makes SwitchToProbeSocket() retire the *new*
    // socket instead of the *old* one, leaving every subsequent send to fail
    // with EBADF.
    void SetSocket(common::SocketHandle sock) {
        if (probe_sock_.fd > 0 && sock.fd == probe_sock_.fd) {
            return;
        }
        sock_ = sock;
    }

    /**
     * @brief Install the migration probe socket (preferred while non-zero).
     */
    void SetProbeSocket(common::SocketHandle sock) { probe_sock_ = sock; }

    common::SocketHandle GetProbeSocket() const { return probe_sock_; }

    /**
     * @brief Promote the probe socket to primary (migration succeeded).
     *
     * @return The retired primary socket. The caller owns it and must
     *         unregister and close its fd — this class never closes sockets.
     */
    [[nodiscard]] common::SocketHandle SwitchToProbeSocket();

    /**
     * @brief Abandon the probe socket (migration failed).
     *
     * The caller is responsible for unregistering and closing the fd it
     * obtained from GetProbeSocket() beforehand.
     */
    void ClearProbeSocket() { probe_sock_ = common::SocketHandle(0, 0); }

    /**
     * @brief The socket outbound datagrams go out on. Sole implementation of
     *        the "prefer probe socket while migrating" rule.
     */
    common::SocketHandle GetActiveSocket() const { return (probe_sock_.fd > 0) ? probe_sock_ : sock_; }

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
    AmpBudgetCheck amp_budget_check_;
    AmpBudgetQuery amp_budget_query_;

    // Set by the packet-building paths when the datagram being assembled carries
    // an Initial packet; consumed (and cleared) by Emit(). See
    // MarkDatagramCarriesInitial().
    bool datagram_carries_initial_{false};

    std::shared_ptr<ISender> sender_;
    std::vector<std::shared_ptr<NetPacket>>* send_sink_{nullptr};

    // fd + family: the family travels with the fd so the send path never
    // re-derives it from the kernel. fd == 0 means "no socket installed yet".
    common::SocketHandle sock_;
    common::SocketHandle probe_sock_;

    // The id 0 is reserved for "no datagram open"; first real id is 1.
    uint64_t next_datagram_id_{1};
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_DATAGRAM_EMITTER
