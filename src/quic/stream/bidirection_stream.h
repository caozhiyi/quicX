#ifndef QUIC_STREAM_BIDIRECTION_STREAM
#define QUIC_STREAM_BIDIRECTION_STREAM

#include "quic/stream/recv_stream.h"
#include "quic/stream/send_stream.h"

namespace quicx {
namespace quic {

class BidirectionStream:
    public virtual SendStream,
    public virtual RecvStream {
public:
    BidirectionStream(std::shared_ptr<common::BlockMemoryPool> alloter, uint64_t init_data_limit, uint64_t id = 0);
    virtual ~BidirectionStream();

    // reset the stream
    virtual void Reset(uint64_t error);

    virtual void Close(uint64_t error = 0);

    virtual uint32_t OnFrame(std::shared_ptr<IFrame> frame);

    virtual IStream::TrySendResult TrySendData(IFrameVisitor* visitor);
};

}
}

#endif