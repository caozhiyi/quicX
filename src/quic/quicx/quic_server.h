#ifndef QUIC_QUICX_QUIC_CLIENT
#define QUIC_QUICX_QUIC_CLIENT

#include "quic/include/if_quic_server.h"
#include "quic/quicx/master_with_thread.h"
#include "common/network/if_event_loop.h"

namespace quicx {
namespace quic {

class QuicServer:
    public IQuicServer {
public:
    QuicServer(const QuicTransportParams& params);
    virtual ~QuicServer();

    // thread_num: io thread number
    virtual bool Init(const QuicServerConfig& config) override;

    // join io threads
    virtual void Join() override;

    // distroy quic libary, release all resource
    // all connections will be closed
    virtual void Destroy() override;

    // add a timer
    virtual void AddTimer(uint32_t timeout_ms, std::function<void()> cb) override;

    // listen and accept a quic connection
    virtual bool ListenAndAccept(const std::string& ip, uint16_t port) override;

    // called when connection state changed, like connected, disconnected, etc
    // user should set this callback before connection or listen and accept, otherwise, connection will be lost
    virtual void SetConnectionStateCallBack(connection_state_callback cb) override;

private:
    QuicTransportParams params_;
    std::shared_ptr<common::IEventLoop> master_event_loop_;
    std::shared_ptr<MasterWithThread> master_;
    connection_state_callback connection_state_cb_;
    std::unordered_map<std::string, std::shared_ptr<IWorker>> worker_map_;
};

}
}

#endif