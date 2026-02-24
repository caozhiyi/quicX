#ifndef QUIC_QUICX_WORKER_SERVER
#define QUIC_QUICX_WORKER_SERVER

#include <string>

#include "common/network/if_event_loop.h"

#include "quic/connection/retry_token_manager.h"
#include "quic/include/if_quic_server.h"
#include "quic/quicx/connection_rate_monitor.h"
#include "quic/quicx/ip_rate_limiter.h"
#include "quic/quicx/worker.h"
#include "quic/udp/if_sender.h"

namespace quicx {
namespace quic {

// a normal worker
class ServerWorker: public Worker {
public:
    ServerWorker(const QuicServerConfig& config, std::shared_ptr<TLSCtx> ctx, std::shared_ptr<ISender> sender,
        const QuicTransportParams& params, connection_state_callback connection_handler,
        std::shared_ptr<common::IEventLoop> event_loop);
    virtual ~ServerWorker();

    virtual bool InnerHandlePacket(PacketParseResult& packet_info) override;

protected:
    void SendVersionNegotiatePacket(const common::Address& addr, int32_t socket);

    /**
     * @brief Send a Retry packet to the client for address validation
     * @param addr Client address
     * @param socket Socket to send on
     * @param original_dcid Original destination connection ID from client's Initial
     * @param original_scid Original source connection ID from client's Initial
     * @return true if Retry packet was sent successfully
     */
    bool SendRetryPacket(const common::Address& addr, int32_t socket, const ConnectionID& original_dcid,
        const ConnectionID& original_scid, uint32_t version);

    /**
     * @brief Check if a Retry token is valid
     * @param token Token from client's Initial packet
     * @param addr Client address
     * @param original_dcid Original destination connection ID
     * @return true if token is valid
     */
    bool ValidateRetryToken(const std::string& token, const common::Address& addr, ConnectionID& out_original_dcid);

    /**
     * @brief Determine if a Retry packet should be sent.
     *
     * Implements RFC 9000 Section 8.1 address validation strategy:
     * - kNever:     Always returns false (performance priority)
     * - ALWAYS:    Returns true if no valid token (security priority)
     * - kSelective: Returns true based on connection rate and IP behavior
     *
     * @param has_valid_token Whether the client provided a valid Retry token
     * @param client_addr Client's network address
     * @return true if Retry should be sent, false to accept connection directly
     */
    bool ShouldSendRetry(bool has_valid_token, const common::Address& client_addr);

private:
    std::string server_alpn_;

    /** Retry policy configuration */
    RetryPolicy retry_policy_;
    SelectiveRetryConfig selective_config_;
    uint32_t retry_token_lifetime_;

    /** Retry infrastructure */
    std::shared_ptr<RetryTokenManager> retry_token_manager_;
    std::shared_ptr<ConnectionRateMonitor> rate_monitor_;
    std::shared_ptr<IPRateLimiter> ip_limiter_;
};

}  // namespace quic
}  // namespace quicx

#endif