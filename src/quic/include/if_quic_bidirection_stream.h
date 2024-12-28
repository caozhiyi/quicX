#ifndef QUIC_INCLUDE_IF_QUIC_BIDRECTION_STREAM
#define QUIC_INCLUDE_IF_QUIC_BIDRECTION_STREAM

#include "quic/include/if_quic_recv_stream.h"
#include "quic/include/if_quic_send_stream.h"

namespace quicx {
namespace quic {

/*
 bidirection stream interface
*/
class IQuicBidirectionStream:
    public IQuicRecvStream,
    public IQuicSendStream {
public:
    IQuicBidirectionStream() {}
    virtual ~IQuicBidirectionStream() {}

};

}
}

#endif