#ifndef QUIC_CONNECTION_IF_CONNECTION
#define QUIC_CONNECTION_IF_CONNECTION

#include <functional>
#include <memory>
#include <quicx/quic/if_quic_connection.h>
#include <vector>

#include "common/network/address.h"
#include "common/network/socket_handle.h"

#include "quic/connection/connection_id.h"
#include "quic/crypto/tls/type.h"
#include "quic/packet/if_packet.h"

namespace quicx {
namespace common {
class QlogTrace;
}

namespace quic {

// Forward declarations
class ISender;
class IConnection;
class NetPacket;

// Aggregates all connection-level callbacks to reduce constructor parameter count
struct ConnectionCallbacks {
    std::function<void(std::shared_ptr<IConnection>)> active_connection_cb;
    std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb;
    std::function<void(ConnectionID&, std::shared_ptr<IConnection>)> add_conn_id_cb;
    std::function<void(ConnectionID&)> retire_conn_id_cb;
    std::function<void(std::shared_ptr<IConnection>, uint64_t, const std::string&)> connection_close_cb;
};

class IConnection: public IQuicConnection {
public:
    IConnection(const ConnectionCallbacks& callbacks);
    virtual ~IConnection();

    //*************** outside interface ***************//
    virtual void SetUserData(void* user_data) override { user_data_ = user_data; }
    virtual void* GetUserData() override { return user_data_; }

    virtual void SetContext(std::shared_ptr<void> context) override { context_ = std::move(context); }
    virtual std::shared_ptr<void> GetContext() override { return context_; }

    virtual void GetRemoteAddr(std::string& addr, uint32_t& port) override;

    // close the connection gracefully. that means all the streams will be closed gracefully.
    virtual void Close() override = 0;

    // close the connection immediately. that means all the streams will be closed immediately.
    virtual void Reset(uint32_t error_code) override = 0;

    // create a new stream, only supported send stream and bidirection stream.
    virtual std::shared_ptr<IQuicStream> MakeStream(StreamDirection type) override = 0;
    // set the callback function to handle the stream state change.
    virtual void SetStreamStateCallBack(stream_state_callback cb) override { stream_state_cb_ = cb; }
    // add a timer, implementation in BaseConnection
    virtual uint64_t AddTimer(timer_callback callback, uint32_t timeout_ms, bool periodic = false) override = 0;
    // remove a timer, implementation in BaseConnection
    virtual void RemoveTimer(uint64_t timer_id) override = 0;

    //*************** inner interface ***************//
    virtual void AddTransportParam(const QuicTransportParams& tp_config) = 0;
    virtual uint64_t GetConnectionIDHash() = 0;
    // Get all local CID hashes for this connection (for cleanup on close)
    virtual std::vector<uint64_t> GetAllLocalCIDHashes() = 0;
    // Main send interface. Emits up to `budget` back-to-back packets in a
    // single call, reusing the per-call setup work (encryption-level scheduler
    // context look-up, cryptographer pointer fetch, packet-builder fixed
    // fields) across the inner iterations. Returns the number of packets
    // actually emitted (>=0, <= budget).
    //
    // There is deliberately no single-packet TrySend() sibling: having both
    // meant tests drove a path production never took (the one-packet variant
    // lacked Initial+Handshake coalescing, so handshake behaviour under test
    // diverged from reality). Callers wanting one packet pass budget=1.
    virtual int TrySendBurst(int budget) = 0;
    // Set sender for direct packet transmission (used by tests)
    virtual void SetSender(std::shared_ptr<ISender> /*sender*/) {}
    // Install (or clear with nullptr) a per-drain-round batch sink. When
    // installed, built NetPackets are appended to the sink rather than handed
    // straight to sender_->Send(), so the worker can issue a single
    // sendmmsg(2) over the whole drain round. Default implementation is a
    // no-op for connection types that never go through Worker::ProcessSend
    // (e.g. mock/test connections).
    virtual void SetSendSink(std::vector<std::shared_ptr<NetPacket>>* /*sink*/) {}
    // Process decoded QUIC packets from a single UDP datagram.
    // |datagram_size| is the raw UDP payload size (including PADDING) and
    // credits the RFC 9000 §8.1 anti-amplification budget before dispatch.
    virtual void OnPackets(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets, uint32_t datagram_size) = 0;
    // provide ECN value for the next OnPackets call (per received datagram)
    virtual void SetPendingEcn(uint8_t ecn) = 0;
    virtual EncryptionLevel GetCurEncryptionLevel() = 0;

    // Production-side query: is the 0-RTT (early data) write key currently
    // installed? Worker uses this to decide whether to deliver a freshly
    // promoted connection as kEarlyConnection vs kConnectionCreate. Kept on
    // the IConnection interface (rather than reaching into a concrete type)
    // because the worker only sees IConnection here.
    //
    // Note: this *replaces* the previous GetCryptographerForTest(kEarlyData)
    // that worker used as a stand-in. The "ForTest" accessor only ever
    // belonged to test code — it has been moved off the IConnection vtable
    // into BaseConnection as a non-virtual member; tests reach it through
    // the concrete ClientConnection/ServerConnection types they already hold.
    virtual bool HasEarlyDataWriteKey() const = 0;

    // connection transfer between threads
    virtual void ThreadTransferBefore() = 0;
    virtual void ThreadTransferAfter() = 0;

    // peer address
    virtual void SetPeerAddress(const common::Address& addr);
    virtual void SetPeerAddress(const common::Address&& addr);
    virtual const common::Address& GetPeerAddress();
    // observe peer address from incoming datagrams; default no-op
    virtual void OnObservedPeerAddress(const common::Address& addr) { (void)addr; }
    // notify candidate-path datagram was received with its address and size; default no-op
    virtual void OnCandidatePathDatagramReceived(const common::Address& addr, uint32_t bytes) {
        (void)addr;
        (void)bytes;
    }
    // get destination address for next datagram; default current peer address
    virtual common::Address AcquireSendAddress() { return peer_addr_; }

    // ==================== Connection Migration (RFC 9000 Section 9) ====================

    // Simple migration API (for interop tests - system chooses new address)
    virtual bool InitiateMigration() override { return false; }

    // Full migration API (production use - specify new local address)
    virtual MigrationResult InitiateMigrationTo(const std::string& local_ip, uint16_t local_port = 0) override {
        (void)local_ip;
        (void)local_port;
        return MigrationResult::kFailedInvalidState;
    }

    // Set callback for migration events
    virtual void SetMigrationCallback(migration_callback cb) override { migration_cb_ = cb; }

    // Get current local address
    virtual void GetLocalAddr(std::string& addr, uint32_t& port) override;

    // Check if migration is supported (peer didn't disable it)
    virtual bool IsMigrationSupported() const override { return false; }

    // Check if migration is in progress
    virtual bool IsMigrationInProgress() const override { return false; }

    // Get the qlog trace bound to this connection (null if qlog is disabled).
    // Internal-only accessor: deliberately kept off the public IQuicConnection
    // interface so the internal qlog types don't leak into the public API.
    virtual std::shared_ptr<common::QlogTrace> GetQlogTrace() const = 0;

    // Internal: Get local address bound to socket
    virtual bool GetLocalAddressFromSocket(int32_t sockfd, common::Address& addr);

    // The active socket is owned by DatagramEmitter (see BaseConnection), so
    // this is virtual rather than a field write here. The handle carries the
    // socket's address family from its creation site; fd <= 0 means "none".
    virtual void SetSocket(common::SocketHandle sock) = 0;

    // The socket the connection currently sends from (the emitter's active
    // socket). Lets the server worker detect a datagram that arrived on a
    // different local listener (preferred_address) — a new path per RFC 9000
    // §9 even when the peer's source address is unchanged.
    virtual common::SocketHandle GetActiveSocket() const { return common::SocketHandle(-1, 0); }

    // The peer's datagram arrived on a different local socket than the one
    // this connection has been using (e.g. a client migrating to our
    // preferred_address listener). Triggers path validation on the new
    // path. Default no-op.
    virtual void OnLocalSocketAddressChanged() {}

    // Record the local socket a peer datagram arrived ON and report whether
    // it is the first time this connection sees that socket. First-seen
    // means the peer moved to a new local listener (preferred_address) —
    // a new path per RFC 9000 §9. Deliberately keyed on the RECEIVE socket,
    // never the send fd: a NAT-rebind probe swaps the send fd after every
    // migration, which must not look like a new listener. Default: never
    // new (clients have a single socket).
    virtual bool OnRxSocket(int32_t sockfd) {
        (void)sockfd;
        return false;
    }

    // Set callback to register a new socket with the receiver (for connection
    // migration). The handle carries the family from the socket's creation
    // site so the receiver never has to probe the kernel.
    using RegisterSocketCallback = std::function<bool(common::SocketHandle sock)>;
    virtual void SetRegisterSocketCallback(RegisterSocketCallback cb) { register_socket_cb_ = cb; }

    // Set callback to remove a socket from the receiver's poll set. Needed to
    // retire the old socket after a successful migration and the probe socket
    // after a failed one; without it retired fds stay armed in the event loop.
    using UnregisterSocketCallback = std::function<bool(int32_t sockfd)>;
    virtual void SetUnregisterSocketCallback(UnregisterSocketCallback cb) { unregister_socket_cb_ = cb; }

protected:
    void* user_data_;
    std::shared_ptr<void> context_;
    common::Address peer_addr_;
    common::Address local_addr_;  // Cached local address
    // callback
    std::function<void(ConnectionID&, std::shared_ptr<IConnection>)> add_conn_id_cb_;
    std::function<void(ConnectionID&)> retire_conn_id_cb_;
    std::function<void(std::shared_ptr<IConnection>)> active_connection_cb_;
    std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb_;
    std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)> connection_close_cb_;
    migration_callback migration_cb_;                // Migration event callback
    RegisterSocketCallback register_socket_cb_;      // Register socket with receiver for migration
    UnregisterSocketCallback unregister_socket_cb_;  // Remove a retired socket from the poll set

    stream_state_callback stream_state_cb_;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_IF_CONNECTION