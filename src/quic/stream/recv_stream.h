#ifndef QUIC_STREAM_RECV_STREAM
#define QUIC_STREAM_RECV_STREAM

#include <list>
#include <string>
#include <functional>
#include <unordered_map>
#include "quic/stream/if_recv_stream.h"
#include "quic/stream/if_frame_visitor.h"

namespace quicx {
namespace quic {

class RecvStream:
    public virtual IRecvStream {
public:
    RecvStream(std::shared_ptr<common::BlockMemoryPool>& alloter, uint64_t init_data_limit, uint64_t id = 0);
    ~RecvStream();

    // close the stream
    virtual void Reset(uint64_t error);

    // process recv frames
    virtual uint32_t OnFrame(std::shared_ptr<IFrame> frame);

    // try generate data to send
    virtual IStream::TrySendResult TrySendData(IFrameVisitor* visitor);

protected:
    uint32_t OnStreamFrame(std::shared_ptr<IFrame> frame);
    void OnStreamDataBlockFrame(std::shared_ptr<IFrame> frame);
    void OnResetStreamFrame(std::shared_ptr<IFrame> frame);

protected:
    uint64_t final_offset_;
    // peer send data limit
    uint32_t local_data_limit_;
    // next except data offset
    uint64_t except_offset_;
    std::shared_ptr<common::IBufferChains> recv_buffer_;
    std::unordered_map<uint64_t, std::shared_ptr<IFrame>> out_order_frame_;
};

}
}

#endif