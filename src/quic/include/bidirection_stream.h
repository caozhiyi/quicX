#ifndef QUIC_INCLUDE_BIDRECTION_STREAM
#define QUIC_INCLUDE_BIDRECTION_STREAM

#include <string>
#include <cstdint>
#include "quic/include/send_stream.h"
#include "quic/include/recv_stream.h"

namespace quicx {
namespace quic {

class QuicxBidirectionStream: 
    public virtual QuicxSendStream,
    public virtual QuicxRecvStream {
public:
    QuicxBidirectionStream() {}
    virtual ~QuicxBidirectionStream() {}

};

}
}

#endif