#ifndef QUIC_STREAM_TYPE
#define QUIC_STREAM_TYPE

#include <cstdint>

namespace quicx {
namespace quic {

enum class StreamState: uint16_t {
    kUnknown     = 0,
    // sending stream states
    kReady       = 0x0001,
    kSend        = 0x0002,
    kDataSent    = 0x0004,
    kResetSent   = 0x0008,
    
    // receiving stream states
    kRecv        = 0x0010,
    kSizeKnown   = 0x0020,
    kDataRead    = 0x0040,
    kResetRead   = 0x0080,

    // common termination states
    kDataRecvd   = 0x0100,
    kResetRecvd  = 0x0200,
};

}
}

#endif