#ifndef QUIC_QUICX_QUIC_CLIENT
#define QUIC_QUICX_QUIC_CLIENT

#include "quic/include/type.h"
#include "quic/include/if_quic_client.h"
#include "quic/quicx/master_with_thread.h"

namespace quicx {
namespace quic {

class QuicClient:
    public IQuicClient {
public:
    QuicClient(const QuicTransportParams& params);
    virtual ~QuicClient();
    // thread_num: io thread number
    virtual bool Init(const QuicClientConfig& config) override;

    // add a timer
    virtual void AddTimer(uint32_t timeout_ms, std::function<void()> cb) override;

    // join io threads
    virtual void Join() override;

    // distroy quic libary, release all resource
    // all connections will be closed
    virtual void Destroy() override;

    // connect to a quic server with a specific resumption session (DER bytes) for this connection
    virtual bool Connection(const std::string& ip, uint16_t port,
        const std::string& alpn, int32_t timeout_ms, const std::string& resumption_session_der) override;

    // called when connection state changed, like connected, disconnected, etc
    // user should set this callback before connection or listen and accept, otherwise, connection will be lost
    virtual void SetConnectionStateCallBack(connection_state_callback cb) override;

private:
    ThreadMode thread_mode_;
    QuicTransportParams params_;
    std::shared_ptr<MasterWithThread> master_;
    connection_state_callback connection_state_cb_;
    std::unordered_map<std::string, std::shared_ptr<IWorker>> worker_map_;
};

}
}

#endif