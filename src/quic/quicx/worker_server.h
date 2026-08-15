#ifndef QUIC_QUICX_WORKER_SERVER
#define QUIC_QUICX_WORKER_SERVER

#include <string>
#include <unordered_map>

#include <quicx/common/if_event_loop.h>

#include <quicx/quic/if_quic_server.h>
#include "quic/connection/retry_token_manager.h"
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

    /**
     * @brief Override: on handshake completion, also cancel the handshake
     * watchdog timer registered in InnerHandlePacket. If we do not cancel it,
     * the captured std::shared_ptr<ServerConnection> keeps the connection
     * alive for kHandshakeTimeoutMs (5 s) after the handshake finishes, which
     * shows up as a per-connection RSS residue in short-lived connect/close
     * benchmarks.
     */
    virtual void HandleHandshakeDone(std::shared_ptr<IConnection> conn) override;

    /**
     * @brief Override to additionally purge the handshake watchdog timer
     * bookkeeping for |conn| on close. Without this, a connection that is
     * closed before its handshake finishes would leave an orphan entry in
     * handshake_timers_ that still owns a shared_ptr reference.
     */
    void HandleConnectionClose(std::shared_ptr<IConnection> conn, uint64_t error, const std::string& reason) override;

    // See IWorker::Shutdown(). Purges handshake watchdog timers plus
    // retry/rate-limiter dependencies that hold shared_ptrs to the event
    // loop, so the EventLoop↔Worker refcount cycle can terminate.
    void Shutdown() override;

protected:
    void SendVersionNegotiatePacket(const common::Address& addr, int32_t socket, const uint8_t* client_dcid,
        uint8_t client_dcid_len, const uint8_t* client_scid, uint8_t client_scid_len);

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

    /**
     * @brief Concurrency admission check for a brand-new connection.
     *
     * Rate limiting (rate_monitor_ / ip_limiter_) only bounds how *fast* new
     * connections arrive; it does nothing about how many are alive at once. A peer
     * that stays under the rate threshold can still open connections forever and
     * exhaust memory. This enforces the global and per-IP concurrency caps.
     *
     * @return true if a new connection may be admitted.
     */
    bool CanAcceptNewConnection(const common::Address& client_addr) const;

    /**
     * @brief Send an RFC 9000 §10.3 Stateless Reset for an unroutable packet.
     *
     * Called when a 1-RTT packet arrives for a connection ID we have no state
     * for -- typically a peer still talking to a connection lost to a restart or
     * a crash. Without this the peer keeps retransmitting into the void until its
     * idle timeout expires (up to tens of seconds); the reset ends it at once.
     *
     * The token is recomputed from the connection ID, which is what makes this
     * possible with no connection state at all.
     *
     * @param triggering_packet_size Size of the datagram that prompted the reset.
     *        The reset MUST be smaller, so that two endpoints that both reset
     *        cannot ping-pong ever-larger packets at each other (§10.3).
     * @return true if a reset was sent.
     */
    bool SendStatelessReset(const common::Address& addr, int32_t socket, const ConnectionID& dcid,
        uint32_t triggering_packet_size);

    /** Bookkeeping for the per-IP concurrency cap. */
    void OnConnectionAdmitted(const common::Address& client_addr);
    void OnConnectionReleased(const std::shared_ptr<IConnection>& conn);

private:
    std::string server_alpn_;

    /** Concurrency caps; 0 disables the corresponding check. */
    uint32_t max_connections_per_worker_;
    uint32_t max_connections_per_ip_;

    /** Live connection count per source IP, and each connection's source IP. */
    std::unordered_map<std::string, uint32_t> ip_conn_count_;
    std::unordered_map<IConnection*, std::string> conn_source_ip_;

    /** Retry policy configuration */
    RetryPolicy retry_policy_;
    SelectiveRetryConfig selective_config_;
    uint32_t retry_token_lifetime_;

    /** Retry infrastructure */
    std::shared_ptr<RetryTokenManager> retry_token_manager_;
    std::shared_ptr<ConnectionRateMonitor> rate_monitor_;
    std::shared_ptr<IPRateLimiter> ip_limiter_;

    /**
     * @brief Handshake watchdog timers, keyed by the (still-in-connecting_set_)
     * connection shared_ptr. Entries are removed and the underlying timer is
     * cancelled in HandleHandshakeDone() as soon as the handshake finishes.
     *
     * Without this, the lambda passed to AddTimer() captures the connection
     * by value and keeps it alive for kHandshakeTimeoutMs (5 s) after the
     * handshake completes, which inflates per-connection RSS and delays
     * BaseConnection destruction in connect/close benchmarks.
     */
    std::unordered_map<std::shared_ptr<IConnection>, common::Timer> handshake_timers_;
};

}  // namespace quic
}  // namespace quicx

#endif