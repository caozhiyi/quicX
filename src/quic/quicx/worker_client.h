#ifndef QUIC_QUICX_CLIENT_WORKER
#define QUIC_QUICX_CLIENT_WORKER

#include "quic/quicx/worker.h"

namespace quicx {
namespace quic {

// a normal worker
class ClientWorker: public Worker {
public:
    ClientWorker(const QuicConfig& config, std::shared_ptr<TLSCtx> ctx, std::shared_ptr<ISender> sender,
        const QuicTransportParams& params, connection_state_callback connection_handler, std::shared_ptr<common::IEventLoop> event_loop);
    virtual ~ClientWorker();

    virtual void Connect(const std::string& ip, uint16_t port, const std::string& alpn, int32_t timeout_ms,
        const std::string& resumption_session_der = "");

private:
    virtual bool InnerHandlePacket(PacketParseResult& packet_info) override;
    void HandleConnectionTimeout(std::shared_ptr<IConnection> conn);
};

}  // namespace quic
}  // namespace quicx

#endif