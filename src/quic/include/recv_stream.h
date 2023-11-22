#ifndef QUIC_INCLUDE_RECV_STREAM
#define QUIC_INCLUDE_RECV_STREAM

#include <string>
#include <cstdint>
#include "quic/include/stream.h"

namespace quicx {
namespace quic {

class QuicxRecvStream: 
    virtual public QuicxStream {
public:
    QuicxRecvStream() {}
    virtual ~QuicxRecvStream() {}

    virtual void SetStreamReadCallBack(stream_read_call_back cb) = 0;
};

}
}

#endif