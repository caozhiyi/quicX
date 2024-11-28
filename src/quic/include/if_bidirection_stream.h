#ifndef QUIC_INCLUDE_IF_BIDRECTION_STREAM
#define QUIC_INCLUDE_IF_BIDRECTION_STREAM

#include "quic/include/if_recv_stream.h"
#include "quic/include/if_send_stream.h"

namespace quicx {
namespace quic {

/*
 bidirection stream interface
*/
class IBidirectionStream:
    public virtual IRecvStream,
    public virtual ISendStream {
public:
    IBidirectionStream() {}
    virtual ~IBidirectionStream() {}

};

}
}

#endif