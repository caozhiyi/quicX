#ifndef QUIC_QUICX_PROCESSOR_CLIENT
#define QUIC_QUICX_PROCESSOR_CLIENT

#include "quic/quicx/processor_base.h"

namespace quicx {
namespace quic {

/*
 client processor
*/
class ProcessorClient:
    public ProcessorBase {
public:
    ProcessorClient(std::shared_ptr<TLSCtx> ctx,
        const QuicTransportParams& params,
        connection_state_callback connection_handler);
    virtual ~ProcessorClient();

    virtual void Connect(const std::string& ip, uint16_t port,
        const std::string& alpn, int32_t timeout_ms);
private:
    bool HandlePacket(std::shared_ptr<INetPacket> packet);
    void HandleConnectionTimeout(std::shared_ptr<IConnection> conn);
};

}
}

#endif