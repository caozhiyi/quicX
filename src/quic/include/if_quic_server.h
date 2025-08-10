#ifndef QUIC_INCLUDE_IF_QUIC_SERVER
#define QUIC_INCLUDE_IF_QUIC_SERVER

#include "quic/include/type.h"

namespace quicx {
namespace quic {

/*
 a quic server libary interface.
 user can use this interface to create a quic server.
 a quic server instance manage io threads, every one connection lives in a single thread whole life time.
*/
struct QuicServerConfig {
    std::string cert_file_ = "";
    std::string key_file_ = "";
    const char* cert_pem = nullptr;
    const char* key_pem = nullptr;
    std::string alpn_ = "";

    QuicConfig config_;
};

class IQuicServer {
public:
    IQuicServer() {}
    virtual ~IQuicServer() {}

    // init quic libary
    // thread_num: io thread number
    virtual bool Init(const QuicServerConfig& config) = 0;

    // join io threads
    virtual void Join() = 0;

    // distroy quic libary, release all resource
    // all connections will be closed
    virtual void Destroy() = 0;

    // add a timer
    virtual void AddTimer(uint32_t timeout_ms, std::function<void()> cb) = 0;

    // listen and accept a quic connection
    virtual bool ListenAndAccept(const std::string& ip, uint16_t port) = 0;

    // called when connection state changed, like connected, disconnected, etc
    // user should set this callback before connection or listen and accept, otherwise, connection will be lost
    virtual void SetConnectionStateCallBack(connection_state_callback cb) = 0;

    static std::shared_ptr<IQuicServer> Create(const QuicTransportParams& params = DEFAULT_QUIC_TRANSPORT_PARAMS);
};

}
}

#endif