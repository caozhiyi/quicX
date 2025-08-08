#ifndef QUIC_QUICX_WORKER_SERVER
#define QUIC_QUICX_WORKER_SERVER

#include "quic/quicx/worker.h"

namespace quicx {
namespace quic {

// a normal worker
class ServerWorker:
    public Worker {
public:
    ServerWorker(std::shared_ptr<TLSCtx> ctx,
        const QuicTransportParams& params,
        connection_state_callback connection_handler);
    virtual ~ServerWorker();

    virtual bool InnerHandlePacket(PacketInfo& packet_info) override;

protected:
    void SendVersionNegotiatePacket(common::Address& addr);
};

}
}

#endif