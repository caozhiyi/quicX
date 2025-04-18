#ifndef QUIC_QUICX_PROCESSOR_BASE
#define QUIC_QUICX_PROCESSOR_BASE

#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <condition_variable>
#include "quic/udp/if_sender.h"
#include "common/timer/timer.h"
#include "quic/udp/if_receiver.h"
#include "quic/crypto/tls/tls_ctx.h"
#include "quic/quicx/thread_processor.h"
#include "quic/connection/if_connection.h"
#include "quic/quicx/connection_transfor.h"
#include "common/thread/thread_with_queue.h"

namespace quicx {
namespace quic {

/*
 message dispatcher processor
*/
class ProcessorBase:
    public ThreadProcessor  {
public:
    ProcessorBase(std::shared_ptr<TLSCtx> ctx,
        const QuicTransportParams& params,
        connection_state_callback connection_handler);
    virtual ~ProcessorBase();

    virtual void Process();
    virtual void AddReceiver(uint64_t socket_fd);
    virtual void AddReceiver(const std::string& ip, uint16_t port);
    virtual void AddTimer(uint32_t interval_ms, timer_callback cb);

protected:
    void ProcessRecv(uint32_t timeout_ms);
    void ProcessTimer();
    void ProcessSend();

    bool InitPacketCheck(std::shared_ptr<IPacket> packet);
    static bool DecodeNetPakcet(std::shared_ptr<INetPacket> net_packet,
        std::vector<std::shared_ptr<IPacket>>& packets, uint8_t* &cid, uint16_t& len);

    void HandleHandshakeDone(std::shared_ptr<IConnection> conn);
    void HandleActiveSendConnection(std::shared_ptr<IConnection> conn);
    void HandleAddConnectionId(uint64_t cid_hash, std::shared_ptr<IConnection> conn);
    void HandleRetireConnectionId(uint64_t cid_hash);
    void HandleConnectionClose(std::shared_ptr<IConnection> conn, uint64_t error, const std::string& reason);
    virtual void TransferConnection(uint64_t cid_hash, std::shared_ptr<IConnection>& conn);

    virtual bool HandlePacket(std::shared_ptr<INetPacket> packet) = 0;

protected:
    bool do_send_;
    std::string server_alpn_;
    QuicTransportParams params_;

    std::shared_ptr<TLSCtx> ctx_;

    std::function<void(uint64_t)> add_connection_id_cb_;
    std::function<void(uint64_t)> retire_connection_id_cb_;

    std::shared_ptr<common::BlockMemoryPool> alloter_;

    std::unordered_set<std::shared_ptr<IConnection>> active_send_connection_set_;
    std::unordered_set<std::shared_ptr<IConnection>> connecting_set_;

    connection_state_callback connection_handler_;
    thread_local static std::shared_ptr<common::ITimer> time_;
};

}
}

#endif