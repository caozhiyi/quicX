#ifndef QUIC_INCLUDE_IF_QUIC_SERVER
#define QUIC_INCLUDE_IF_QUIC_SERVER

#include "quic/include/type.h"

namespace quicx {

/**
 * @brief Server-side configuration bundle.
 *
 * The server can provide its identity either via PEM strings or filesystem
 * paths. In addition to TLS material, the structure exposes ALPN, ticket TTL
 * and low-level transport configuration.
 */
struct QuicServerConfig {
    /** Filesystem path to the certificate file (PEM). */
    std::string cert_file_ = "";
    /** Filesystem path to the private key file (PEM). */
    std::string key_file_ = "";
    /** In-memory PEM certificate (takes precedence over file path when set). */
    const char* cert_pem_ = nullptr;
    /** In-memory PEM private key. */
    const char* key_pem_ = nullptr;
    /** ALPN label this server advertises (e.g. "hq-29", "h3"). */
    std::string alpn_ = "";
    /** Session ticket validity window in seconds (default: 2 days). */
    uint32_t session_ticket_timeout_ = 172800;

    /** Retry configuration */
    bool force_retry_ = false;            // Force Retry for all connections (testing)
    bool enable_retry_ = true;            // Enable Retry mechanism
    uint32_t retry_token_lifetime_ = 60;  // Retry token lifetime in seconds

    /** Transport/runtime knobs (threading, logging, congestion control, etc.). */
    QuicConfig config_;
};

/**
 * @brief High-level QUIC server interface.
 *
 * Similar to the client counterpart, a concrete implementation hides worker
 * threads and network plumbing. Each accepted connection is pinned to one
 * worker thread to simplify application-level concurrency assumptions.
 */
class IQuicServer {
public:
    IQuicServer() {}
    virtual ~IQuicServer() {}

    /**
     * @brief Initialize the server runtime and spawn worker threads.
     *
     * @param config Composite configuration, including certificates and
     *        transport-level parameters.
     * @return true when initialization succeeds, false otherwise.
     */
    virtual bool Init(const QuicServerConfig& config) = 0;

    /** @brief Join all worker threads (blocking). */
    virtual void Join() = 0;

    /**
     * @brief Stop accepting new connections, close existing ones and release resources.
     */
    virtual void Destroy() = 0;

    /**
     * @brief Schedule a timer on the server's internal event loop.
     *
     * @param timeout_ms Delay in milliseconds.
     * @param cb Callback executed when the timer fires.
     */
    virtual void AddTimer(uint32_t timeout_ms, std::function<void()> cb) = 0;

    /**
     * @brief Start listening on the provided address and accept incoming connections.
     *
     * @param ip Local IP (IPv4 or IPv6 textual form).
     * @param port UDP port.
     * @return true if the listener started successfully.
     */
    virtual bool ListenAndAccept(const std::string& ip, uint16_t port) = 0;

    /**
     * @brief Install a connection state callback for accepted peers.
     *
     * @param cb Observer invoked whenever a connection is created, closed or
     *        encounters a fatal error.
     */
    virtual void SetConnectionStateCallBack(connection_state_callback cb) = 0;

    /**
     * @brief Factory helper returning the default server implementation.
     *
     * @param params Transport parameters announced to connecting clients.
     */
    static std::shared_ptr<IQuicServer> Create(const QuicTransportParams& params = DEFAULT_QUIC_TRANSPORT_PARAMS);
};

}  // namespace quicx

#endif