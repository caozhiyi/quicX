#ifndef QUIC_QUICX_QUIC_CLIENT
#define QUIC_QUICX_QUIC_CLIENT

#include "quic/quicx/quic.h"
#include "quic/include/if_quic_client.h"

namespace quicx {
namespace quic {

class QuicClient:
    public IQuicClient,
    public Quic {
public:
    QuicClient(const QuicTransportParams& params);
    virtual ~QuicClient();
    // thread_num: io thread number
    virtual bool Init(uint16_t thread_num = 1, LogLevel level = LogLevel::kNull);

    // join io threads
    virtual void Join();

    // distroy quic libary, release all resource
    // all connections will be closed
    virtual void Destroy();

    // add a timer
    virtual void AddTimer(uint32_t timeout_ms, std::function<void()> cb) override;

    // connect to a quic server
    virtual bool Connection(const std::string& ip, uint16_t port,
        const std::string& alpn, int32_t timeout_ms);

    // called when connection state changed, like connected, disconnected, etc
    // user should set this callback before connection or listen and accept, otherwise, connection will be lost
    virtual void SetConnectionStateCallBack(connection_state_callback cb);
};

}
}

#endif