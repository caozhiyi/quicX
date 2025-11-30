#ifndef QUIC_INCLUDE_TYPE
#define QUIC_INCLUDE_TYPE

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "common/include/if_buffer_read.h"
#include "common/include/type.h"

namespace quicx {

/**
 * @brief Direction of a QUIC stream.
 */
enum class StreamDirection : uint8_t {
    kSend = 0x01,  //!< Locally initiated send-only stream.
    kRecv = 0x02,  //!< Remotely initiated receive-only stream.
    kBidi = 0x03,  //!< Bidirectional stream supporting send + receive.
};

/**
 * @brief Controls how the library orchestrates master/worker loops.
 */
enum class ThreadMode : uint8_t {
    kSingleThread = 0x00,  //!< Master and worker state machines share one thread.
    kMultiThread = 0x01,   //!< Dedicated master thread plus N worker threads.
};

/**
 * @brief Runtime knobs shared by clients and servers.
 */
struct QuicConfig {
    ThreadMode thread_mode_ = ThreadMode::kSingleThread;  //!< Threading strategy.
    uint16_t worker_thread_num_ = 2;                      //!< Number of worker threads when in multi-thread mode.
    LogLevel log_level_ = LogLevel::kNull;                //!< Minimum log level emitted by the stack.

    bool enable_ecn_ = false;   //!< Toggle ECN handling.
    bool enable_0rtt_ = false;  //!< Allow 0-RTT data when tickets are available.
};

/**
 * @brief Transport parameters exchanged during the QUIC handshake.
 *
 * Exposed here so applications can customize flow-control, migration, idle
 * timeout, and other core transport behaviors.
 */
struct QuicTransportParams {
    std::string original_destination_connection_id_ = "";
    uint32_t max_idle_timeout_ms_ = 120000;  // 2 minutes
    std::string stateless_reset_token_ = "";
    uint32_t max_udp_payload_size_ = 1472;  // 1500 - 28

    // RFC 9000 Section 4.1: Flow control limits
    // Increased from 14KB to prevent flow control blocking with modern congestion windows
    // These values allow congestion control to operate effectively without being
    // artificially limited by flow control
    uint32_t initial_max_data_ = 10 * 1024 * 1024;                    // 10MB connection-level
    uint32_t initial_max_stream_data_bidi_local_ = 1 * 1024 * 1024;   // 1MB per stream (local->remote)
    uint32_t initial_max_stream_data_bidi_remote_ = 1 * 1024 * 1024;  // 1MB per stream (remote->local)
    uint32_t initial_max_stream_data_uni_ = 1 * 1024 * 1024;          // 1MB for unidirectional streams

    uint32_t initial_max_streams_bidi_ = 100;  // Increased from 20 to support concurrent requests
    uint32_t initial_max_streams_uni_ = 100;   // Increased from 20 for consistency
    uint32_t ack_delay_exponent_ms_ = 3;
    uint32_t max_ack_delay_ms_ = 25;
    bool disable_active_migration_ = false;
    std::string preferred_address_ = "";
    uint32_t active_connection_id_limit_ = 3;
    std::string initial_source_connection_id_ = "";
    std::string retry_source_connection_id_ = "";
};
static const QuicTransportParams DEFAULT_QUIC_TRANSPORT_PARAMS;

/**
 * @brief Lifecycle events delivered to connection callbacks.
 */
enum class ConnectionOperation : uint32_t { kConnectionCreate = 0x00, kConnectionClose = 0x01 };

class IQuicStream;
class IQuicConnection;
class IBidirectionStream;

/**
 * @brief Notifies applications about connection-level state changes.
 *
 * @param conn Connection instance whose state changed.
 * @param operation Whether the connection was created or closed.
 * @param error Application/transport error code (0 on success).
 * @param reason Human-readable reason string.
 */
typedef std::function<void(
    std::shared_ptr<IQuicConnection> conn, ConnectionOperation operation, uint32_t error, const std::string& reason)>
    connection_state_callback;

/**
 * @brief Streams lifecycle callback.
 *
 * Called when streams are created, closed or encounter an error.
 */
typedef std::function<void(std::shared_ptr<IQuicStream> stream, uint32_t error)> stream_state_callback;

/**
 * @brief Read-side callback invoked when data becomes available.
 *
 * @param data Buffer referencing the readable bytes.
 * @param is_last Whether the FIN bit was observed with this chunk.
 * @param error Transport-level error (0 on success).
 */
typedef std::function<void(std::shared_ptr<IBufferRead> data, bool is_last, uint32_t error)> stream_read_callback;

/**
 * @brief Write-side callback invoked after a send attempt.
 *
 * @param length Number of bytes written or acknowledged.
 * @param error Non-zero on failure.
 */
typedef std::function<void(uint32_t length, uint32_t error)> stream_write_callback;

/** @brief Generic timer callback used by IQuicClient/IQuicServer. */
typedef std::function<void()> timer_callback;

/**
 * @brief Callback invoked when a stream is created.
 *
 * @param stream The newly created stream.
 */
typedef std::function<void(std::shared_ptr<IQuicStream>)> stream_creation_callback;

}  // namespace quicx

#endif