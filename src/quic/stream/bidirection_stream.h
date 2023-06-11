#ifndef QUIC_STREAM_BIDIRECTION_STREAM
#define QUIC_STREAM_BIDIRECTION_STREAM

#include "quic/stream/recv_stream.h"
#include "quic/stream/send_stream.h"

namespace quicx {

class BidirectionStream:
    public virtual SendStream,
    public virtual RecvStream {
public:
    BidirectionStream(std::shared_ptr<BlockMemoryPool> alloter, uint64_t id = 0);
    virtual ~BidirectionStream();

    // reset the stream
    virtual void Reset(uint64_t error);

    virtual void Close(uint64_t error = 0);

    virtual void OnFrame(std::shared_ptr<IFrame> frame);

    virtual IStream::TrySendResult TrySendData(IFrameVisitor* visitor);
};

}

#endif