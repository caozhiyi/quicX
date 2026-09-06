#ifndef QUIC_INCLUDE_IF_QUIC_CONNECTION
#define QUIC_INCLUDE_IF_QUIC_CONNECTION

#include <memory>
#include <quicx/quic/type.h>
#include <string>

namespace quicx {

/**
 * @brief Represents a live QUIC connection.
 *
 * The interface allows applications to bind metadata, create streams and close
 * the transport in a controlled fashion. Implementations guarantee thread-safe
 * hand-off to the worker thread that owns the connection.
 */
class IQuicConnection {
public:
    IQuicConnection() {}
    virtual ~IQuicConnection() {}

    /**
     * @brief Attach opaque user data to the connection.
     *
     * Ownership remains with the caller; the connection simply stores the raw
     * pointer for later retrieval.
     */
    virtual void SetUserData(void* user_data) = 0;
    virtual void* GetUserData() = 0;

    /**
     * @brief Bind a shared-ownership context object to the connection.
     *
     * Unlike SetUserData (raw pointer, caller-owned), this stores a
     * std::shared_ptr<void>, so the bound object's lifetime is tied to the
     * connection: it is destroyed when the connection object is destroyed.
     * This lets an application layer (e.g. an HTTP/3 connection) attach its
     * per-connection state directly to the transport connection without a
     * separate side table keyed by connection identity.
     */
    virtual void SetContext(std::shared_ptr<void> context) = 0;

    /**
     * @brief Retrieve the context bound via SetContext.
     */
    virtual std::shared_ptr<void> GetContext() = 0;

    /**
     * @brief Convenience typed accessor: static_pointer_cast of GetContext().
     */
    template <typename T>
    std::shared_ptr<T> GetContextAs() {
        return std::static_pointer_cast<T>(GetContext());
    }

    /**
     * @brief Query the peer's address as observed by the transport.
     *
     * @param addr Filled with the string representation of the remote IP.
     * @param port Filled with the remote UDP port.
     */
    virtual void GetRemoteAddr(std::string& addr, uint32_t& port) = 0;

    /**
     * @brief Close the connection gracefully.
     *
     * Oustanding streams get a chance to flush their send buffers; FIN frames
     * are exchanged before the connection shuts down.
     */
    virtual void Close() = 0;

    /**
     * @brief Abort the connection immediately and notify the peer with an error.
     *
     * @param error_code Application-defined reason sent to the remote side.
     */
    virtual void Reset(uint32_t error_code) = 0;

    /**
     * @brief Create a new application stream.
     *
     * Only unidirectional-sender and bidirectional streams can be opened
     * locally. Receive-only streams are delivered via callbacks.
     *
     * @param type Desired stream direction.
     */
    virtual std::shared_ptr<IQuicStream> MakeStream(StreamDirection type) = 0;

    /**
     * @brief Create a new application stream asynchronously.
     *
     * Only unidirectional-sender and bidirectional streams can be opened
     * locally. Receive-only streams are delivered via callbacks.
     *
     * @param type Desired stream direction.
     * @param callback Callback invoked when the stream is created.
     */
    virtual bool MakeStreamAsync(StreamDirection type, stream_creation_callback callback) = 0;

    /**
     * @brief Install a callback that reports stream lifecycle changes.
     *
     * @param cb Callback invoked when streams are created, closed or error out.
     */
    virtual void SetStreamStateCallBack(stream_state_callback cb) = 0;

    /**
     * @brief Schedule a timer callback to execute after a delay.
     *
     * @param callback Function to invoke when timer expires.
     * @param timeout_ms Delay in milliseconds before callback is invoked.
     * @param periodic If true, the timer is re-armed automatically with the
     *     same timeout_ms after every fire, so the callback need not re-schedule
     *     itself. Defaults to false (one-shot).
     * @return Timer ID that can be used to cancel the timer.
     */
    virtual uint64_t AddTimer(timer_callback callback, uint32_t timeout_ms, bool periodic = false) = 0;

    /**
     * @brief Cancel a previously scheduled timer.
     *
     * @param timer_id Timer ID returned by AddTimer.
     */
    virtual void RemoveTimer(uint64_t timer_id) = 0;

    /**
     * @brief Check if the connection is in a terminating state.
     *
     * @return true if connection is Closing, Draining, or Closed.
     */
    virtual bool IsTerminating() const = 0;

    // ==================== Connection Migration (RFC 9000 Section 9) ====================

    /**
     * @brief Initiate connection migration (client-side only, simple API).
     *
     * This is a convenience wrapper for interop tests that delegates to
     * InitiateMigrationTo() with the current local IP and a system-chosen port.
     *
     * The migration will:
     * - Keep the same local IP address
     * - Bind to a new ephemeral port (system-chosen)
     * - Create a new socket and switch to it
     * - Rotate DCID and perform path validation
     *
     * @return true if migration was successfully initiated, false otherwise.
     *
     * @note For production use, prefer InitiateMigrationTo() which provides
     *       detailed error codes and explicit address control.
     *
     * @note Thread-safe. Migration state (paths, connection IDs, sockets,
     *       timers) is owned by the connection's event-loop thread, so a call
     *       from any other thread is dispatched onto that loop and blocks
     *       until the loop reports the outcome. Consequences:
     *       - Do not hold a lock that your own connection/stream callbacks
     *         also acquire; the loop thread may need it to make progress.
     *         Copy what you need out from under the lock, then call.
     *       - Calling from inside a callback (already on the loop thread)
     *         runs inline with no extra latency.
     *       - If the loop is stopped or wedged the call gives up after a
     *         few seconds and returns false rather than blocking forever.
     */
    virtual bool InitiateMigration() = 0;

    /**
     * @brief Initiate connection migration to a specific local address (client-side only).
     *
     * RFC 9000 Section 9: Client-initiated connection migration.
     * This creates a new socket bound to the specified local address,
     * rotates the DCID, and initiates path validation.
     *
     * @param local_ip   New local IP address to migrate to.
     * @param local_port New local port; if 0, system chooses an ephemeral port.
     * @return MigrationResult indicating success or failure reason.
     *
     * @note Thread-safe, with the same dispatch semantics and locking caveat
     *       as InitiateMigration(). Returns kFailedTimeout if the event loop
     *       never picks the request up, kFailedInvalidState if the connection
     *       no longer has a loop.
     */
    virtual MigrationResult InitiateMigrationTo(const std::string& local_ip, uint16_t local_port = 0) = 0;

    /**
     * @brief Set callback for migration events.
     *
     * The callback is invoked when:
     * - Client-initiated migration completes (success or failure)
     * - NAT rebinding is detected and path validation completes
     *
     * @param cb Callback to invoke on migration events.
     */
    virtual void SetMigrationCallback(migration_callback cb) = 0;

    /**
     * @brief Get current local address of the connection.
     *
     * @param addr Filled with the local IP string.
     * @param port Filled with the local UDP port.
     */
    virtual void GetLocalAddr(std::string& addr, uint32_t& port) = 0;

    /**
     * @brief Check if active migration is supported.
     *
     * Migration is not supported if peer sent disable_active_migration transport parameter.
     *
     * @return true if active migration is allowed.
     */
    virtual bool IsMigrationSupported() const = 0;

    /**
     * @brief Check if a migration/path validation is currently in progress.
     *
     * @return true if migration is ongoing.
     */
    virtual bool IsMigrationInProgress() const = 0;
};

}  // namespace quicx

#endif  // QUIC_INCLUDE_IF_QUIC_CONNECTION