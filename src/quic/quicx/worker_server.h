#ifndef QUIC_QUICX_WORKER_SERVER
#define QUIC_QUICX_WORKER_SERVER

#include <string>
#include "quic/quicx/worker.h"
#include "quic/udp/if_sender.h"
#include "quic/include/if_quic_server.h"

namespace quicx {
namespace quic {

// a normal worker
class ServerWorker: public Worker {
public:
    ServerWorker(const QuicServerConfig& config, std::shared_ptr<TLSCtx> ctx, std::shared_ptr<ISender> sender,
        const QuicTransportParams& params, connection_state_callback connection_handler);
    virtual ~ServerWorker();

    virtual bool InnerHandlePacket(PacketParseResult& packet_info) override;

protected:
    void SendVersionNegotiatePacket(const common::Address& addr, int32_t socket);

private:
    std::string server_alpn_;
};

}  // namespace quic
}  // namespace quicx

#endif