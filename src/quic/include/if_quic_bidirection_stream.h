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
    public virtual IQuicStream {
public:
    IQuicBidirectionStream() {}
    virtual ~IQuicBidirectionStream() {}

    virtual StreamDirection GetDirection() = 0;
    virtual uint64_t GetStreamID() = 0;

    // close the stream gracefully, the stream will be closed after all data transported.
    virtual void Close() = 0;

    // close the stream immediately, the stream will be closed immediately even if there are some data inflight.
    // error code will be sent to the peer.
    virtual void Reset(uint32_t error) = 0;

    // send data to peer, return the number of bytes sended.
    virtual int32_t Send(uint8_t* data, uint32_t len) = 0;
    virtual int32_t Send(std::shared_ptr<common::IBufferRead> buffer) = 0;

    // called when data is ready to send, that means the data is in the send buffer.
    // called in the send thread, so do not do any blocking operation.
    // if you don't care about send data detail, you may not set the callback.
    virtual void SetStreamWriteCallBack(stream_write_callback cb) = 0;

    // when there are some data received, the callback function will be called.
    // the callback function will be called in the recv thread. so you should not do any blocking operation in the callback function.
    // you should set the callback function firstly, otherwise the data received will be discarded.
    virtual void SetStreamReadCallBack(stream_read_callback cb) = 0;
};

}
}

#endif