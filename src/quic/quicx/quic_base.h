#ifndef QUIC_QUICX_QUIC_BASE
#define QUIC_QUICX_QUIC_BASE

#include <memory>
#include <vector>

#include "quic/include/type.h"
#include "quic/quicx/if_processor.h"
#include "quic/quicx/thread_processor.h"
#include "quic/crypto/tls/tls_client_ctx.h"

namespace quicx {
namespace quic {

class QuicBase {
public:
    QuicBase();
    virtual ~QuicBase();

    virtual bool Init(uint16_t thread_num = 1);

    virtual void Join();

    virtual void Destroy();

    virtual void SetConnectionStateCallBack(connection_state_callback cb);

protected:
    std::shared_ptr<TLSCtx> tls_ctx_;
    std::vector<std::shared_ptr<ThreadProcessor>> processors_;

    connection_state_callback connection_state_cb_;
};

}
}

#endif