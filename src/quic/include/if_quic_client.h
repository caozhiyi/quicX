#ifndef QUIC_INCLUDE_IF_QUIC_CLIENT
#define QUIC_INCLUDE_IF_QUIC_CLIENT

#include "quic/include/type.h"

namespace quicx {
namespace quic {

/*
 a quic client libary interface.
 user can use this interface to create a quic client.
 a quic client instance manage io threads, every one connection lives in a single thread whole life time.
*/
class IQuicClient {
public:
    IQuicClient() {}
    virtual ~IQuicClient() {}

    // init quic libary
    // thread_num: io thread number
    virtual bool Init(uint16_t thread_num = 1, LogLevel level = LogLevel::kNull) = 0;

    // join io threads
    virtual void Join() = 0;

    // distroy quic libary, release all resource
    // all connections will be closed
    virtual void Destroy() = 0;

    // add timer callback
    virtual void AddTimer(uint32_t interval_ms, timer_callback cb) = 0;

    // connect to a quic server
    virtual bool Connection(const std::string& ip, uint16_t port,
        const std::string& alpn, int32_t timeout_ms) = 0;

    // called when connection state changed, like connected, disconnected, etc
    // user should set this callback before connection or listen and accept, otherwise, connection will be lost
    virtual void SetConnectionStateCallBack(connection_state_callback cb) = 0;

    static std::shared_ptr<IQuicClient> Create(const QuicTransportParams& params = DEFAULT_QUIC_TRANSPORT_PARAMS);
};

}
}

#endif