#ifndef QUIC_INCLUDE_IF_STREAM
#define QUIC_INCLUDE_IF_STREAM

#include "quic/include/type.h"

namespace quicx {
namespace quic {

/*
 stream interface
 indicates a quic stream
*/
class IQuicStream {
public:
    IQuicStream() {}
    virtual ~IQuicStream() {}

    virtual void Reset(uint32_t error) = 0;
    virtual StreamDirection GetDirection() = 0;
    virtual uint64_t GetStreamID() = 0;
};

}
}

#endif