#ifndef QUIC_QUICX_QUIC_SERVER
#define QUIC_QUICX_QUIC_SERVER

#include <string>
#include <memory>
#include <vector>

#include "quic/quicx/quic_base.h"
#include "quic/quicx/if_processor.h"
#include "quic/include/if_quic_server.h"
#include "quic/crypto/tls/tls_ctx_server.h"

namespace quicx {
namespace quic {

class QuicServer:
    public IQuicServer,
    public QuicBase {
public:
    QuicServer();
    virtual ~QuicServer();

    virtual bool Init(const std::string& cert_file, const std::string& key_file, const std::string& alpn, uint16_t thread_num = 1, LogLevel level = LL_NULL);
    virtual bool Init(const char* cert_pem, const char* key_pem, const std::string& alpn, uint16_t thread_num = 1, LogLevel level = LL_NULL);

    virtual void Join();

    virtual void Destroy();

    virtual void AddTimer(uint32_t interval_ms, timer_callback cb) { /* TODO: implement this */ }

    virtual bool ListenAndAccept(const std::string& ip, uint16_t port);

    virtual void SetConnectionStateCallBack(connection_state_callback cb);
};

}
}

#endif