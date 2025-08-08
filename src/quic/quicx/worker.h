#ifndef QUIC_QUICX_WORKER
#define QUIC_QUICX_WORKER

#include <vector>
#include <memory>
#include <functional>
#include <unordered_set>

#include "quic/include/type.h"
#include "quic/udp/if_sender.h"
#include "quic/quicx/if_worker.h"
#include "quic/crypto/tls/tls_ctx.h"
#include "quic/connection/if_connection.h"
#include "common/thread/thread_with_queue.h"
#include "common/structure/thread_safe_block_queue.h"

namespace quicx {
namespace quic {

class Worker:
    public IWorker,
    public common::ThreadWithQueue<std::function<void()>> {
public:
    Worker(std::shared_ptr<TLSCtx> ctx,
        const QuicTransportParams& params,
        connection_state_callback connection_handler);
    virtual ~Worker();

    // Initialize the worker
    virtual void Init(std::shared_ptr<IConnectionIDNotify> connection_id_notify) override;
    // Destroy the worker
    virtual void Destroy() override;

    // Weakup the worker
    virtual void Weakup() override;

    // Join the worker
    virtual void Join() override;

    // Get the current thread id
    virtual std::thread::id GetCurrentThreadId() override;

    // Handle packets
    virtual void HandlePacket(PacketInfo& packet_info) override;

protected:
    void Run() override;

    void ProcessTimer();
    void ProcessSend();
    void ProcessRecv();
    void ProcessTask();

    virtual bool InnerHandlePacket(PacketInfo& packet_info) = 0;
    bool InitPacketCheck(std::shared_ptr<IPacket> packet);

    void HandleAddConnectionId(ConnectionID& cid, std::shared_ptr<IConnection> conn);
    void HandleRetireConnectionId(ConnectionID& cid);
    void HandleHandshakeDone(std::shared_ptr<IConnection> conn);
    void HandleActiveSendConnection(std::shared_ptr<IConnection> conn);
    void HandleConnectionClose(std::shared_ptr<IConnection> conn, uint64_t error, const std::string& reason);

protected:
    common::ThreadSafeBlockQueue<PacketInfo> packet_queue_;


protected:
    std::weak_ptr<IConnectionIDNotify> connection_id_notify_;

    bool do_send_;
    std::string server_alpn_;
    QuicTransportParams params_;

    std::shared_ptr<ISender> sender_;

    std::shared_ptr<TLSCtx> ctx_;
    std::shared_ptr<common::ITimer> time_;

    std::function<void(uint64_t)> add_connection_id_cb_;
    std::function<void(uint64_t)> retire_connection_id_cb_;

    std::shared_ptr<common::BlockMemoryPool> alloter_;

    std::unordered_set<std::shared_ptr<IConnection>> active_send_connection_set_;
    std::unordered_set<std::shared_ptr<IConnection>> connecting_set_;
    std::unordered_map<uint64_t, std::shared_ptr<IConnection>> conn_map_; // all connections

    connection_state_callback connection_handler_;
};

}
}

#endif