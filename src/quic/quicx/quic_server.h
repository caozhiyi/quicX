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
    virtual bool Init(const std::string& cert_file, const std::string& key_file, const std::string& alpn,
        uint16_t thread_num = 1, LogLevel level = LogLevel::kNull);
    virtual bool Init(const char* cert_pem, const char* key_pem, const std::string& alpn,
        uint16_t thread_num = 1, LogLevel level = LogLevel::kNull);

    // join io threads
    virtual void Join();

    // distroy quic libary, release all resource
    // all connections will be closed
    virtual void Destroy();

    // listen and accept a quic connection
    virtual bool ListenAndAccept(const std::string& ip, uint16_t port);

    // called when connection state changed, like connected, disconnected, etc
    // user should set this callback before connection or listen and accept, otherwise, connection will be lost
    virtual void SetConnectionStateCallBack(connection_state_callback cb);
};

}
}

#endif