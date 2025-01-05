#ifndef QUIC_INCLUDE_TYPE
#define QUIC_INCLUDE_TYPE

#include <memory>
#include <cstdint>
#include <functional>
#include "common/buffer/if_buffer_read.h"

namespace quicx {
namespace quic {

class IQuicStream;
class IQuicConnection;
class IBidirectionStream;

enum StreamDirection: uint8_t {
    SD_SEND = 0x01, // send stream
    SD_RECV = 0x02, // recv stream
    SD_BIDI = 0x03, // bidirection stream
};

enum AlpnType: uint8_t {
    AT_HTTP3     = 1,
    AT_TRANSPORT = 2,
};

// connection state callback, call this callback when connection state changed, like connected, disconnected, etc.
// conn: connection instance which state changed
typedef std::function<void(std::shared_ptr<IQuicConnection> conn, uint32_t error)> connection_state_callback;

// stream state callback, call this callback when stream state changed, like created, closed, etc.
// stream: stream instance which state changed
typedef std::function<void(std::shared_ptr<IQuicStream> stream, uint32_t error)> stream_state_callback;

// stream read callback, call this callback when stream get data from peer
// data: data buffer
typedef std::function<void(std::shared_ptr<common::IBufferRead> data, uint32_t error)> stream_read_callback;

// stream write callback, call this callback when stream write data ready to send
// length: data length
typedef std::function<void(uint32_t length, uint32_t error)> stream_write_callback;

}
}

#endif