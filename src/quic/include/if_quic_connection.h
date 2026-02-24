#ifndef QUIC_INCLUDE_IF_QUIC_CONNECTION
#define QUIC_INCLUDE_IF_QUIC_CONNECTION

#include <string>
#include "quic/include/type.h"

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
     * @return Timer ID that can be used to cancel the timer.
     */
    virtual uint64_t AddTimer(timer_callback callback, uint32_t timeout_ms) = 0;

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
     */
    virtual bool InitiateMigration() { return false; }

    /**
     * @brief Initiate connection migration to a specific local address (client-side only).
     *
     * RFC 9000 Section 9: Client-initiated connection migration.
     * This creates a new socket bound to the specified local address,
     * rotates the DCID, and initiates path validation.
     *
     * @param local_addr New local address to migrate to (IP:port).
     *                   If port is 0, system chooses an ephemeral port.
     * @return MigrationResult indicating success or failure reason.
     */
    virtual MigrationResult InitiateMigrationTo(const std::string& local_ip, uint16_t local_port = 0) {
        (void)local_ip;
        (void)local_port;
        return MigrationResult::kFailedInvalidState;
    }

    /**
     * @brief Set callback for migration events.
     *
     * The callback is invoked when:
     * - Client-initiated migration completes (success or failure)
     * - NAT rebinding is detected and path validation completes
     *
     * @param cb Callback to invoke on migration events.
     */
    virtual void SetMigrationCallback(migration_callback cb) { (void)cb; }

    /**
     * @brief Get current local address of the connection.
     *
     * @param addr Filled with the local IP string.
     * @param port Filled with the local UDP port.
     */
    virtual void GetLocalAddr(std::string& addr, uint32_t& port) {
        addr = "";
        port = 0;
    }

    /**
     * @brief Check if active migration is supported.
     *
     * Migration is not supported if peer sent disable_active_migration transport parameter.
     *
     * @return true if active migration is allowed.
     */
    virtual bool IsMigrationSupported() const { return false; }

    /**
     * @brief Check if a migration/path validation is currently in progress.
     *
     * @return true if migration is ongoing.
     */
    virtual bool IsMigrationInProgress() const { return false; }
};

}  // namespace quicx

#endif