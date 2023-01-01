#ifndef QUIC_STREAM_BIDIRECTION_STREAM
#define QUIC_STREAM_BIDIRECTION_STREAM

#include "quic/stream/recv_stream.h"
#include "quic/stream/send_stream.h"

namespace quicx {

class BidirectionStream:
    public ISendStream,
    public IRecvStream {
public:
    BidirectionStream();
    virtual ~BidirectionStream();

    virtual int32_t Send(uint8_t* data, uint32_t len);

    // reset the stream
    virtual void Reset(uint64_t err);
 
    // end the stream
    virtual void Close();

    virtual void HandleFrame(std::shared_ptr<IFrame> frame);
};

}

#endif