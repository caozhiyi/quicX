#ifndef QUIC_QUICX_QUIC_BASE
#define QUIC_QUICX_QUIC_BASE

#include <memory>
#include <vector>
#include <thread>
#include <unordered_map>

#include "quic/include/type.h"
#include "quic/quicx/if_processor.h"
#include "quic/quicx/processor_base.h"
#include "quic/crypto/tls/tls_ctx_client.h"

namespace quicx {
namespace quic {

class QuicBase {
public:
    QuicBase(const QuicTransportParams& params);
    virtual ~QuicBase();

    virtual void Join();

    virtual void Destroy();

    virtual void SetConnectionStateCallBack(connection_state_callback cb);

    void AddTimer(uint32_t interval_ms, timer_callback cb);

protected:
    void InitLogger(LogLevel level);

protected:
    std::shared_ptr<TLSCtx> tls_ctx_;
    std::unordered_map<std::thread::id, std::shared_ptr<ProcessorBase>> processors_map_;

    connection_state_callback connection_state_cb_;
    QuicTransportParams params_;
};

}
}

#endif