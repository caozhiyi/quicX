#ifndef QUIC_QUICX_QUIC_CLIENT
#define QUIC_QUICX_QUIC_CLIENT

#include "quic/quicx/quic.h"
#include "quic/include/if_quic_server.h"

namespace quicx {
namespace quic {

class QuicServer:
    public IQuicServer,
    public Quic {
public:
    QuicServer(const QuicTransportParams& params);
    virtual ~QuicServer();

    // thread_num: io thread number
    virtual bool Init(const QuicServerConfig& config);

    // join io threads
    virtual void Join();

    // distroy quic libary, release all resource
    // all connections will be closed
    virtual void Destroy();

    // add a timer
    virtual void AddTimer(uint32_t timeout_ms, std::function<void()> cb) override;

    // listen and accept a quic connection
    virtual bool ListenAndAccept(const std::string& ip, uint16_t port);

    // called when connection state changed, like connected, disconnected, etc
    // user should set this callback before connection or listen and accept, otherwise, connection will be lost
    virtual void SetConnectionStateCallBack(connection_state_callback cb);
};

}
}

#endif