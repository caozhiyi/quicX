#ifndef QUIC_INCLUDE_IF_QUIC_CLIENT
#define QUIC_INCLUDE_IF_QUIC_CLIENT

#include "quic/include/type.h"

namespace quicx {
namespace quic {

struct QuicClientConfig {
    bool enable_session_cache_ = false;
    std::string session_cache_path_ = "./session_cache";

    QuicConfig config_;
};

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
    virtual bool Init(const QuicClientConfig& config) = 0;

    // join io threads
    virtual void Join() = 0;

    // distroy quic libary, release all resource
    // all connections will be closed
    virtual void Destroy() = 0;

    // add a timer
    virtual void AddTimer(uint32_t timeout_ms, std::function<void()> cb) = 0;

    // connect to a quic server
    virtual bool Connection(const std::string& ip, uint16_t port,
        const std::string& alpn, int32_t timeout_ms) = 0;

    // connect to a quic server with a specific resumption session (DER bytes) for this connection
    // passing non-empty session enables 0-RTT on resumption if ticket allows
    virtual bool Connection(const std::string& ip, uint16_t port,
        const std::string& alpn, int32_t timeout_ms, const std::string& resumption_session_der) = 0;

    // called when connection state changed, like connected, disconnected, etc
    // user should set this callback before connection or listen and accept, otherwise, connection will be lost
    virtual void SetConnectionStateCallBack(connection_state_callback cb) = 0;

    static std::shared_ptr<IQuicClient> Create(const QuicTransportParams& params = DEFAULT_QUIC_TRANSPORT_PARAMS);
};

}
}

#endif