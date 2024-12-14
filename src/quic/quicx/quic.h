#ifndef QUIC_QUICX_QUICX_IMPL
#define QUIC_QUICX_QUIC

#include <memory>
#include <vector>
#include "quic/include/quicx.h"
#include "common/thread/thread.h"
#include "quic/crypto/tls/tls_ctx.h"
#include "quic/quicx/receiver_interface.h"
#include "quic/quicx/processor_interface.h"

namespace quicx {
namespace quic {

class Quic:
    public IQuic {
public:
    Quic();
    virtual ~Quic();

    virtual bool Init(uint16_t thread_num);

    virtual void Join();

    virtual void Destroy();

    virtual bool Connection(const std::string& ip, uint16_t port, int32_t timeout_ms);

    virtual bool ListenAndAccept(const std::string& ip, uint16_t port);

    virtual void SetConnectionStateCallBack(connection_state_callback cb);
private:
    std::shared_ptr<TLSCtx> ctx_;
    std::shared_ptr<IReceiver> receiver_;
    std::vector<std::shared_ptr<IProcessor>> processors_;
};

}
}

#endif