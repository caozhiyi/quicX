#ifndef QUIC_QUICX_QUIC_CLIENT
#define QUIC_QUICX_QUIC_CLIENT

#include <memory>
#include <vector>

#include "quic/quicx/if_processor.h"
#include "quic/include/if_quic_client.h"
#include "quic/crypto/tls/tls_client_ctx.h"

namespace quicx {
namespace quic {

class QuicClient:
    public IClientQuic {
public:
    QuicClient();
    virtual ~QuicClient();

    virtual bool Init(uint16_t thread_num = 1);

    virtual void Join();

    virtual void Destroy();

    virtual bool Connection(const std::string& ip, uint16_t port, int32_t timeout_ms);

    virtual void SetConnectionStateCallBack(connection_state_callback cb);

private:
    std::shared_ptr<TLSClientCtx> tls_ctx_;
    std::vector<std::shared_ptr<IProcessor>> processors_;
};

}
}

#endif