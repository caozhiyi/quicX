#ifndef QUIC_QUICX_WORKER
#define QUIC_QUICX_WORKER

#include <functional>
#include <memory>
#include <unordered_set>

#include "common/network/if_event_loop.h"
#include "common/structure/double_buffer.h"

#include "quic/connection/if_connection.h"
#include "quic/crypto/tls/tls_ctx.h"
#include "quic/include/type.h"
#include "quic/quicx/if_worker.h"
#include "quic/udp/if_sender.h"

namespace quicx {
namespace quic {

class Worker: public IWorker {
public:
    Worker(const QuicConfig& config, std::shared_ptr<TLSCtx> ctx, std::shared_ptr<ISender> sender,
        const QuicTransportParams& params, connection_state_callback connection_handler,
        std::shared_ptr<common::IEventLoop> event_loop);
    virtual ~Worker();

    // Get the worker id
    virtual std::string GetWorkerId() override;
    // Handle packets
    virtual void HandlePacket(PacketParseResult& packet_info) override;

    // process inner packets
    virtual void Process() override;

    // Send packet immediately (bypasses normal flow)
    // Used for immediate ACK sending when encryption level differs from current level
    bool SendImmediate(std::shared_ptr<common::IBuffer> buffer, const common::Address& addr, int32_t socket = -1);

    // Set callback for registering new sockets with the receiver (for connection migration)
    using RegisterSocketCallback = std::function<bool(int32_t sockfd)>;
    void SetRegisterSocketCallback(RegisterSocketCallback cb) { register_socket_cb_ = cb; }

protected:
    void ProcessSend();

    virtual bool InnerHandlePacket(PacketParseResult& packet_info) = 0;
    bool InitPacketCheck(std::shared_ptr<IPacket> packet, uint32_t datagram_size);

    void HandleAddConnectionId(ConnectionID& cid, std::shared_ptr<IConnection> conn);
    void HandleRetireConnectionId(ConnectionID& cid);
    void HandleHandshakeDone(std::shared_ptr<IConnection> conn);
    void HandleActiveSendConnection(std::shared_ptr<IConnection> conn);
    void HandleConnectionClose(std::shared_ptr<IConnection> conn, uint64_t error, const std::string& reason);

protected:
    bool do_send_;
    bool ecn_enabled_;
    bool enable_key_update_;  // RFC 9001: Key Update support
    uint32_t quic_version_;   // QUIC version from config
    std::string worker_id_;
    QuicTransportParams params_;

    std::shared_ptr<ISender> sender_;

    std::shared_ptr<TLSCtx> ctx_;

    std::function<void(uint64_t)> add_connection_id_cb_;
    std::function<void(uint64_t)> retire_connection_id_cb_;

    // Double buffer for active send connections
    // Allows concurrent modifications during send processing
    common::DoubleBuffer<std::shared_ptr<IConnection>> active_send_connections_;

    std::unordered_set<std::shared_ptr<IConnection>> connecting_set_;
    std::unordered_map<uint64_t, std::shared_ptr<IConnection>> conn_map_;  // all connections

    connection_state_callback connection_handler_;
    std::shared_ptr<common::IEventLoop> event_loop_;  // Saved EventLoop for cross-thread access
    RegisterSocketCallback register_socket_cb_;  // Register socket with receiver for migration
};

}  // namespace quic
}  // namespace quicx

#endif