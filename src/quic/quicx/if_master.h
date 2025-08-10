#ifndef QUIC_QUICX_IF_MASTER
#define QUIC_QUICX_IF_MASTER

#include <memory>

#include "quic/include/type.h"
#include "quic/connection/connection_id_manager.h"

namespace quicx {
namespace quic {

// Worker manager interface
class IMaster {
public:
    IMaster() {}
    virtual ~IMaster() {}

    // Initialize the msg receiver
    virtual bool InitAsClient(int32_t thread_num, const QuicTransportParams& params, connection_state_callback connection_state_cb) = 0;
    virtual bool InitAsServer(int32_t thread_num, const std::string& cert_file, const std::string& key_file, const std::string& alpn, 
        const QuicTransportParams& params, connection_state_callback connection_state_cb) = 0;
    virtual bool InitAsServer(int32_t thread_num, const char* cert_pem, const char* key_pem, const std::string& alpn, 
        const QuicTransportParams& params, connection_state_callback connection_state_cb) = 0;
    // Destroy the msg receiver
    virtual void Destroy() = 0;

    // Weakup the msg receiver
    virtual void Weakup() = 0;

    // Join the msg receiver
    virtual void Join() = 0;

    // add a timer
    virtual void AddTimer(uint32_t timeout_ms, std::function<void()> cb) = 0;

    // connect to a quic server
    virtual bool Connection(const std::string& ip, uint16_t port,
        const std::string& alpn, int32_t timeout_ms) = 0;
    // connect to a quic server with a specific resumption session (DER bytes) for this connection
    // passing non-empty session enables 0-RTT on resumption if ticket allows
    virtual bool Connection(const std::string& ip, uint16_t port,
        const std::string& alpn, int32_t timeout_ms, const std::string& resumption_session_der) = 0;

    // add listener
    virtual void AddListener(uint64_t listener_sock) = 0;
    virtual void AddListener(const std::string& ip, uint64_t port) = 0;

    // add a new connection id
    virtual void AddConnectionID(ConnectionID& cid) = 0;
    // retire a connection id
    virtual void RetireConnectionID(ConnectionID& cid) = 0;

    static std::shared_ptr<IMaster> MakeMaster();
};

}
}

#endif