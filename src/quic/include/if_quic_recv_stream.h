#ifndef QUIC_INCLUDE_IF_QUIC_RECV_STREAM
#define QUIC_INCLUDE_IF_QUIC_RECV_STREAM

#include "quic/include/if_quic_stream.h"

namespace quicx {
namespace quic {

/*
 recv stream interface, when client make a directional stream, server will get a recv stream object which only can receive data. 
 the first thing you should do is set read callback function when you get the stream.
*/
class IQuicRecvStream:
    public virtual IQuicStream {
public:
    IQuicRecvStream() {}
    virtual ~IQuicRecvStream() {}

    virtual StreamDirection GetDirection() = 0;
    virtual uint64_t GetStreamID() = 0;

    // close the stream immediately, the stream will be closed immediately even if there are some data inflight.
    // error code will be sent to the peer.
    virtual void Reset(uint32_t error) = 0;

    // when there are some data received, the callback function will be called.
    // the callback function will be called in the recv thread. so you should not do any blocking operation in the callback function.
    // you should set the callback function firstly, otherwise the data received will be discarded.
    virtual void SetStreamReadCallBack(stream_read_callback cb) = 0;
};

}
}

#endif