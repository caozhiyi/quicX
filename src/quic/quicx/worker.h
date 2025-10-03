#ifndef QUIC_QUICX_WORKER
#define QUIC_QUICX_WORKER

#include <memory>
#include <functional>
#include <unordered_set>

#include "quic/include/type.h"
#include "quic/udp/if_sender.h"
#include "quic/quicx/if_worker.h"
#include "quic/crypto/tls/tls_ctx.h"
#include "common/network/if_event_loop.h"
#include "quic/connection/if_connection.h"

namespace quicx {
namespace quic {

class Worker:
    public IWorker{
public:
    Worker(const QuicConfig& config, 
        std::shared_ptr<TLSCtx> ctx,
        std::shared_ptr<ISender> sender,
        const QuicTransportParams& params,
        std::shared_ptr<common::IEventLoop> event_loop,
        connection_state_callback connection_handler);
    virtual ~Worker();

    // Get the worker id
    virtual std::string GetWorkerId() override;
    // Handle packets
    virtual void HandlePacket(PacketInfo& packet_info) override;
    // Get the event loop
    virtual std::shared_ptr<common::IEventLoop> GetEventLoop() override;

    // process inner packets
    virtual void Process();

protected:
    void ProcessSend();

    virtual bool InnerHandlePacket(PacketInfo& packet_info) = 0;
    bool InitPacketCheck(std::shared_ptr<IPacket> packet);

    void HandleAddConnectionId(ConnectionID& cid, std::shared_ptr<IConnection> conn);
    void HandleRetireConnectionId(ConnectionID& cid);
    void HandleHandshakeDone(std::shared_ptr<IConnection> conn);
    void HandleActiveSendConnection(std::shared_ptr<IConnection> conn);
    void HandleConnectionClose(std::shared_ptr<IConnection> conn, uint64_t error, const std::string& reason);

    std::unordered_set<std::shared_ptr<IConnection>>& GetReadActiveSendConnectionSet();
    std::unordered_set<std::shared_ptr<IConnection>>& GetWriteActiveSendConnectionSet();
    void SwitchActiveSendConnectionSet();

protected:
    bool do_send_;
    bool ecn_enabled_;
    std::string worker_id_;
    QuicTransportParams params_;

    std::shared_ptr<ISender> sender_;

    std::shared_ptr<TLSCtx> ctx_;

    std::function<void(uint64_t)> add_connection_id_cb_;
    std::function<void(uint64_t)> retire_connection_id_cb_;

    std::shared_ptr<common::BlockMemoryPool> alloter_;

    bool active_send_connection_set_1_is_current_;
    std::unordered_set<std::shared_ptr<IConnection>> active_send_connection_set_1_;
    std::unordered_set<std::shared_ptr<IConnection>> active_send_connection_set_2_;

    std::unordered_set<std::shared_ptr<IConnection>> connecting_set_;
    std::unordered_map<uint64_t, std::shared_ptr<IConnection>> conn_map_; // all connections

    connection_state_callback connection_handler_;
    std::shared_ptr<common::IEventLoop> event_loop_;
};

}
}

#endif