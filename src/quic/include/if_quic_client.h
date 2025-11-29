#ifndef QUIC_INCLUDE_IF_QUIC_CLIENT
#define QUIC_INCLUDE_IF_QUIC_CLIENT

#include "quic/include/type.h"

namespace quicx {

/**
 * @brief Configuration bundle for building a QUIC client.
 *
 * The structure mirrors the knobs exposed by the QUIC stack: the high-level
 * client behavior (session cache) as well as the low-level transport settings
 * that are carried inside `QuicConfig`.
 */
struct QuicClientConfig {
    /** Enable TLS session caching so future connections can attempt 0-RTT. */
    bool enable_session_cache_ = false;
    /** Directory used to persist session tickets when caching is enabled. */
    std::string session_cache_path_ = "./session_cache";

    /** Embedded QUIC transport/runtime configuration. */
    QuicConfig config_;
};

/**
 * @brief High-level QUIC client interface.
 *
 * A concrete implementation hides thread management, packet processing and the
 * crypto handshake. Each connection established through the client is pinned to
 * a single worker thread for its entire lifetime, which simplifies callback
 * ordering guarantees for applications.
 */
class IQuicClient {
public:
    IQuicClient() {}
    virtual ~IQuicClient() {}

    /**
     * @brief Initialize the client runtime and spawn worker threads.
     *
     * Call exactly once before creating connections. After a successful call,
     * the client owns the background event loops and is ready to connect.
     *
     * @param config Composite configuration that describes threading, logging,
     *        TLS session cache behavior and QUIC transport limits.
     * @return true on success, false if initialization failed.
     */
    virtual bool Init(const QuicClientConfig& config) = 0;

    /**
     * @brief Join all worker threads.
     *
     * Blocks until every worker thread exits. Typical usage is to call Join()
     * during shutdown after Destroy() has been issued to stop the loops.
     */
    virtual void Join() = 0;

    /**
     * @brief Tear down the client runtime and close all outstanding connections.
     *
     * Invoking Destroy() releases transport resources and requests every active
     * connection to shut down. After Destroy() returns the client can be
     * reinitialized or deleted.
     */
    virtual void Destroy() = 0;

    /**
     * @brief Schedule a timer on the client's internal event loop.
     *
     * The callback will be dispatched on one of the worker threads after the
     * timeout expires.
     *
     * @param timeout_ms Delay in milliseconds.
     * @param cb Callback to execute when the timer fires.
     */
    virtual void AddTimer(uint32_t timeout_ms, std::function<void()> cb) = 0;

    /**
     * @brief Establish a QUIC connection using an explicit resumption ticket.
     *
     * Passing non-empty session bytes allows the client to attempt 0-RTT if the
     * ticket is still valid on the server. The API shape mirrors the basic
     * Connection() call but adds the ticket parameter.
     *
     * @param ip Remote IP (IPv4 or IPv6 textual form).
     * @param port Remote UDP port.
     * @param alpn ALPN identifier (e.g. "h3").
     * @param timeout_ms Connection timeout in milliseconds.
     * @param resumption_session_der Serialized TLS session (DER).
     * @return true if the connect attempt was dispatched, false otherwise.
     */
    virtual bool Connection(const std::string& ip, uint16_t port,
        const std::string& alpn, int32_t timeout_ms, const std::string& resumption_session_der = "", const std::string& server_name = "") = 0;

    /**
     * @brief Register a callback that observes connection-level state changes.
     *
     * Typical events include connection creation, handshake completion, close
     * notifications and errors.
     *
     * @param cb Application callback to invoke for each state update.
     *
     * @note Install the callback before initiating connections; otherwise the
     *       events emitted during handshake might be missed.
     */
    virtual void SetConnectionStateCallBack(connection_state_callback cb) = 0;

    /**
     * @brief Factory helper returning the default client implementation.
     *
     * @param params Transport parameters advertised during the handshake.
     */
    static std::shared_ptr<IQuicClient> Create(const QuicTransportParams& params = DEFAULT_QUIC_TRANSPORT_PARAMS);
};

}

#endif