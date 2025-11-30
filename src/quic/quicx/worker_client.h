#ifndef QUIC_QUICX_CLIENT_WORKER
#define QUIC_QUICX_CLIENT_WORKER

#include <unordered_map>
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
        const std::string& resumption_session_der = "", const std::string& server_name = "");

protected:
    // Override to cancel handshake timeout timer
    void HandleHandshakeDone(std::shared_ptr<IConnection> conn);

private:
    virtual bool InnerHandlePacket(PacketParseResult& packet_info) override;
    void HandleConnectionTimeout(std::shared_ptr<IConnection> conn);

    // Store handshake timeout timers for each connection
    std::unordered_map<std::shared_ptr<IConnection>, uint64_t> handshake_timers_;
};

}  // namespace quic
}  // namespace quicx

#endif