#ifndef QUIC_QUICX_QUIC_CLIENT
#define QUIC_QUICX_QUIC_CLIENT

#include <memory>
#include <vector>

#include "quic/quicx/quic_base.h"
#include "quic/quicx/if_processor.h"
#include "quic/include/if_quic_client.h"
#include "quic/crypto/tls/tls_ctx_client.h"

namespace quicx {
namespace quic {

class QuicClient:
    public IQuicClient,
    public QuicBase {
public:
    QuicClient(const QuicTransportParams& params);
    virtual ~QuicClient();

    virtual bool Init(uint16_t thread_num = 1, LogLevel level = LogLevel::kNull);

    virtual void Join();

    virtual void Destroy();

    virtual void AddTimer(uint32_t interval_ms, timer_callback cb);

    virtual bool Connection(const std::string& ip, uint16_t port,
        const std::string& alpn, int32_t timeout_ms);

    virtual void SetConnectionStateCallBack(connection_state_callback cb);
};

}
}

#endif