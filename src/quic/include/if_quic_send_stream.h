#ifndef QUIC_INCLUDE_IF_QUIC_SEND_STREAM
#define QUIC_INCLUDE_IF_QUIC_SEND_STREAM

#include <string>
#include "quic/include/if_quic_stream.h"

namespace quicx {
namespace quic {

/*
 send stream interface, used to send data to peer.
 who send data create this stream actively, remote peer can't create this stream.
*/
class IQuicSendStream:
    public virtual IQuicStream {
public:
    IQuicSendStream() {}
    virtual ~IQuicSendStream() {}

    // send data to peer, return the number of bytes sended.
    virtual int32_t Send(uint8_t* data, uint32_t len) = 0;
    virtual int32_t Send(std::shared_ptr<common::IBufferRead> buffer) = 0;

    // called when data is ready to send, that means the data is in the send buffer.
    // called in the send thread, so do not do any blocking operation.
    // if you don't care about send data detail, you may not set the callback.
    virtual void SetStreamWriteCallBack(stream_write_callback cb) = 0;
};

}
}

#endif