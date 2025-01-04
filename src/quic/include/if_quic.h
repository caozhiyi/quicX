#ifndef QUIC_INCLUDE_QUIC_QUICX
#define QUIC_INCLUDE_QUIC_QUICX

#include "quic/include/type.h"
#include "quic/include/if_quic_connection.h"
#include "quic/include/if_quic_bidirection_stream.h"

namespace quicx {
namespace quic {

/*
 a quic libary interface.
 user can use this interface to create a quic server or client.
 a quic instance manage io threads, every one connection lives in a single thread whole life time.
*/
class IQuic {
public:
    IQuic() {}
    virtual ~IQuic() {}

    // init quic libary
    // thread_num: io thread number
    virtual bool Init(uint16_t thread_num = 1) = 0;
    virtual bool Init(const std::string& cert_file, const std::string& key_file, uint16_t thread_num = 1) = 0;
    virtual bool Init(const char* cert_pem, const char* key_pem, uint16_t thread_num = 1) = 0;

    // join io threads
    virtual void Join() = 0;

    // distroy quic libary, release all resource
    // all connections will be closed
    virtual void Destroy() = 0;

    // connect to a quic server
    virtual bool Connection(const std::string& ip, uint16_t port, int32_t timeout_ms) = 0;

    // listen and accept a quic connection
    virtual bool ListenAndAccept(const std::string& ip, uint16_t port) = 0;

    // called when connection state changed, like connected, disconnected, etc
    // user should set this callback before connection or listen and accept, otherwise, connection will be lost
    virtual void SetConnectionStateCallBack(connection_state_callback cb) = 0;
};

}
}

#endif