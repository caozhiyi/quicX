#ifndef QUIC_QUICX_SERVER_WORKER
#define QUIC_QUICX_SERVER_WORKER

#include "quic/quicx/worker.h"

namespace quicx {
namespace quic {

// a normal worker
class ClientWorker:
    public Worker {
public:
    ClientWorker(std::shared_ptr<TLSCtx> ctx,
        const QuicTransportParams& params,
        connection_state_callback connection_handler);
    virtual ~ClientWorker();

    virtual void Connect(const std::string& ip, uint16_t port,
        const std::string& alpn, int32_t timeout_ms);
private:
    virtual bool InnerHandlePacket(PacketInfo& packet_info) override;
    void HandleConnectionTimeout(std::shared_ptr<IConnection> conn);
};

}
}

#endif