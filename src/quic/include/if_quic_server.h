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
class IQuicServer {
public:
    IQuicServer() {}
    virtual ~IQuicServer() {}

    // init quic libary
    // thread_num: io thread number
    virtual bool Init(const std::string& cert_file, const std::string& key_file, const std::string& alpn,
        uint16_t thread_num = 1, LogLevel level = LL_NULL) = 0;
    virtual bool Init(const char* cert_pem, const char* key_pem, const std::string& alpn,
        uint16_t thread_num = 1, LogLevel level = LL_NULL) = 0;

    // join io threads
    virtual void Join() = 0;

    // distroy quic libary, release all resource
    // all connections will be closed
    virtual void Destroy() = 0;

    // set timer callback
    virtual void AddTimer(uint32_t interval_ms, timer_callback cb) = 0;

    // listen and accept a quic connection
    virtual bool ListenAndAccept(const std::string& ip, uint16_t port) = 0;

    // called when connection state changed, like connected, disconnected, etc
    // user should set this callback before connection or listen and accept, otherwise, connection will be lost
    virtual void SetConnectionStateCallBack(connection_state_callback cb) = 0;

    static std::shared_ptr<IQuicServer> Create();
};



}
}

#endif