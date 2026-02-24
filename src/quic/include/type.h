#ifndef QUIC_INCLUDE_TYPE
#define QUIC_INCLUDE_TYPE

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "common/include/if_buffer_read.h"
#include "common/include/type.h"
#include "quic/common/version.h"

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
    std::string log_path_ = "./logs";                     //!< Log path.
    
    bool enable_ecn_ = false;         //!< Toggle ECN handling.
    bool enable_0rtt_ = false;        //!< Allow 0-RTT data when tickets are available.
    bool enable_key_update_ = false;  //!< Enable automatic Key Update during connection.
    std::string cipher_suites_ = "";  //!< Cipher suites (e.g. TLS_AES_128_GCM_SHA256).

    //! QUIC version to use (RFC 9000 v1 or RFC 9369 v2).
    //! Default to QUIC v2 (0x6b3343cf) as preferred version.
    uint32_t quic_version_ = quic::kQuicVersion2;

    QlogConfig qlog_config_;  //!< QLog configuration.

    std::string keylog_file_;  //!< Path to SSLKEYLOGFILE for TLS key logging (Wireshark debugging).
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
    // Large initial windows for high-throughput scenarios (file transfer, streaming)
    // These values allow congestion control to operate effectively without being
    // artificially limited by flow control
    uint32_t initial_max_data_ = 64 * 1024 * 1024;                     // 64MB connection-level
    uint32_t initial_max_stream_data_bidi_local_ = 16 * 1024 * 1024;   // 16MB per stream (local->remote)
    uint32_t initial_max_stream_data_bidi_remote_ = 16 * 1024 * 1024;  // 16MB per stream (remote->local)
    uint32_t initial_max_stream_data_uni_ = 16 * 1024 * 1024;          // 16MB for unidirectional streams

    uint32_t initial_max_streams_bidi_ = 200;  // default value
    uint32_t initial_max_streams_uni_ = 200;   // default value
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
enum class ConnectionOperation : uint32_t {
    kConnectionCreate = 0x00,
    kConnectionClose = 0x01,
    kEarlyConnection = 0x02,  //!< 0-RTT early data keys installed; connection usable before handshake completes.
};

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

// ==================== Connection Migration Types (RFC 9000 Section 9) ====================

/**
 * @brief Result of a connection migration attempt.
 */
enum class MigrationResult : uint8_t {
    kSuccess = 0x00,                  //!< Migration completed successfully
    kFailedNoAvailableCID = 0x01,     //!< No available CID from peer for rotation
    kFailedMigrationDisabled = 0x02,  //!< Peer disabled active migration
    kFailedProbeInProgress = 0x03,    //!< Path probe already in progress
    kFailedInvalidState = 0x04,       //!< Connection not in valid state for migration
    kFailedSocketCreation = 0x05,     //!< Failed to create new socket
    kFailedSocketBind = 0x06,         //!< Failed to bind to new local address
    kFailedPathValidation = 0x07,     //!< Path validation failed (no PATH_RESPONSE)
    kFailedTimeout = 0x08,            //!< Migration timed out
};

/**
 * @brief Configuration for connection migration.
 */
struct MigrationConfig {
    bool enable_active_migration_ = true;         //!< Allow client to initiate migration
    bool enable_nat_rebinding_ = true;            //!< Allow automatic NAT rebinding detection
    uint32_t path_validation_timeout_ms_ = 6000;  //!< Path validation timeout (default 6s)
    uint32_t max_probe_retries_ = 5;              //!< Max retries for path validation
    uint32_t probe_initial_delay_ms_ = 100;       //!< Initial probe retry delay
    uint32_t probe_max_delay_ms_ = 2000;          //!< Max probe retry delay (exponential backoff)

    //! If true, automatically detect local address changes and trigger migration
    bool enable_local_address_monitoring_ = false;

    //! Preferred local address for migration (empty = system chooses)
    std::string preferred_local_ip_ = "";
    uint16_t preferred_local_port_ = 0;
};

/**
 * @brief Detailed information about a migration event.
 */
struct MigrationInfo {
    std::string old_local_ip_;                            //!< Previous local IP address
    uint16_t old_local_port_ = 0;                         //!< Previous local port
    std::string new_local_ip_;                            //!< New local IP address
    uint16_t new_local_port_ = 0;                         //!< New local port
    std::string old_peer_ip_;                             //!< Previous peer IP (for NAT rebinding)
    uint16_t old_peer_port_ = 0;                          //!< Previous peer port
    std::string new_peer_ip_;                             //!< New peer IP (for NAT rebinding)
    uint16_t new_peer_port_ = 0;                          //!< New peer port
    uint64_t migration_start_time_ = 0;                   //!< Timestamp when migration started
    uint64_t migration_end_time_ = 0;                     //!< Timestamp when migration completed
    MigrationResult result_ = MigrationResult::kSuccess;  //!< Migration result
    bool is_nat_rebinding_ = false;                       //!< True if this was NAT rebinding (not active migration)
};

/**
 * @brief Callback invoked when connection migration state changes.
 *
 * @param conn Connection that experienced migration.
 * @param info Detailed information about the migration.
 */
typedef std::function<void(std::shared_ptr<IQuicConnection> conn, const MigrationInfo& info)> migration_callback;

}  // namespace quicx

#endif