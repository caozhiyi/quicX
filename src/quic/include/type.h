#ifndef QUIC_INCLUDE_TYPE
#define QUIC_INCLUDE_TYPE

#include <memory>
#include <cstdint>
#include <functional>
#include "common/buffer/buffer_chains_interface.h"

namespace quicx {
namespace quic {
    
class QuicxStream;
class QuicxConnection;

enum StreamType {
    ST_SEND = 0x01, // send stream
    ST_RECV = 0x02, // recv stream
    ST_BIDI = 0x03, // bidirection stream
};

typedef std::function<void(std::shared_ptr<QuicxConnection> conn, uint32_t error)> connection_state_call_back;

typedef std::function<void(std::shared_ptr<QuicxStream> conn, uint32_t error)> stream_state_call_back;
typedef std::function<void(std::shared_ptr<common::IBufferChains> data, uint32_t error)> stream_read_call_back;
typedef std::function<void(uint32_t length, uint32_t error)> stream_write_call_back;


}
}

#endif