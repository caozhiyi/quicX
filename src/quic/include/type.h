#ifndef QUIC_INCLUDE_TYPE
#define QUIC_INCLUDE_TYPE

#include <memory>
#include <cstdint>
#include <functional>
#include "quic/include/if_quic_buffer.h"

namespace quicx {
namespace quic {

class IStream;
class IConnection;
class ISendStream;
class IRecvStream;
class IBidirectionStream;

enum StreamDirection {
    SD_SEND = 0x01, // send stream
    SD_RECV = 0x02, // recv stream
    SD_BIDI = 0x03, // bidirection stream
};

// connection state callback, call this callback when connection state changed, like connected, disconnected, etc.
// conn: connection instance which state changed
typedef std::function<void(std::shared_ptr<IConnection> conn, uint32_t error)> connection_state_callback;

// stream state callback, call this callback when stream state changed, like created, closed, etc.
// stream: stream instance which state changed
typedef std::function<void(std::shared_ptr<IStream> stream, uint32_t error)> stream_state_callback;

// stream read callback, call this callback when stream get data from peer
// data: data buffer
typedef std::function<void(std::shared_ptr<IQuicBuffer> data, uint32_t error)> stream_read_callback;

// stream write callback, call this callback when stream write data ready to send
// length: data length
typedef std::function<void(uint32_t length, uint32_t error)> stream_write_callback;

}
}

#endif