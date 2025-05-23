#ifndef QUIC_INCLUDE_TYPE
#define QUIC_INCLUDE_TYPE

#include <memory>
#include <string>
#include <cstdint>
#include <functional>
#include "common/buffer/if_buffer_read.h"

namespace quicx {
namespace quic {

enum class StreamDirection: uint8_t {
    kSend = 0x01, // send stream
    kRecv = 0x02, // recv stream
    kBidi = 0x03, // bidirection stream
};

// log level
enum class LogLevel: uint8_t {
    kNull   = 0x00, // not print log
    kFatal  = 0x01,
    kError  = 0x02 | kFatal,
    kWarn   = 0x04 | kError,
    kInfo   = 0x08 | kWarn,
    kDebug  = 0x10 | kInfo,
};

struct QuicTransportParams {
    std::string original_destination_connection_id_ = "";
    uint32_t    max_idle_timeout_ms_ = 120000; // 2 minutes
    std::string stateless_reset_token_ = "";
    uint32_t    max_udp_payload_size_ = 1472;  // 1500 - 28
    uint32_t    initial_max_data_ = 1472*10;
    uint32_t    initial_max_stream_data_bidi_local_ = 1472*10;
    uint32_t    initial_max_stream_data_bidi_remote_ = 1472*10;
    uint32_t    initial_max_stream_data_uni_ = 1472*10;
    uint32_t    initial_max_streams_bidi_ = 20;
    uint32_t    initial_max_streams_uni_ = 20;
    uint32_t    ack_delay_exponent_ms_ = 5;
    uint32_t    max_ack_delay_ms_ = 5;
    bool        disable_active_migration_ = false;
    std::string preferred_address_ = "";
    uint32_t    active_connection_id_limit_ = 3;
    std::string initial_source_connection_id_ = "";
    std::string retry_source_connection_id_ = "";
};
static const QuicTransportParams DEFAULT_QUIC_TRANSPORT_PARAMS;

enum class ConnectionOperation: uint32_t {
    kConnectionCreate   = 0x00,
    kConnectionClose    = 0x01
};

class IQuicStream;
class IQuicConnection;
class IBidirectionStream;

// connection state callback, call this callback when connection state changed, like connected, disconnected, etc.
// conn: connection instance which state changed
typedef std::function<void(std::shared_ptr<IQuicConnection> conn, ConnectionOperation operation, uint32_t error, const std::string& reason)> connection_state_callback;

// stream state callback, call this callback when stream state changed, like created, closed, etc.
// stream: stream instance which state changed
typedef std::function<void(std::shared_ptr<IQuicStream> stream, uint32_t error)> stream_state_callback;

// stream read callback, call this callback when stream get data from peer
// data: data buffer
typedef std::function<void(std::shared_ptr<common::IBufferRead> data, uint32_t error)> stream_read_callback;

// stream write callback, call this callback when stream write data ready to send
// length: data length
typedef std::function<void(uint32_t length, uint32_t error)> stream_write_callback;

// timer callback, call this callback when timer expired
typedef std::function<void()> timer_callback;

}
}

#endif