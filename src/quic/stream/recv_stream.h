#ifndef QUIC_STREAM_RECV_STREAM
#define QUIC_STREAM_RECV_STREAM

#include <list>
#include <string>
#include <functional>
#include <unordered_map>
#include "quic/stream/recv_stream_interface.h"
#include "quic/stream/frame_visitor_interface.h"

namespace quicx {
namespace quic {

class RecvStream:
    public virtual IRecvStream {
public:
    RecvStream(std::shared_ptr<common::BlockMemoryPool>& alloter, uint64_t init_data_limit, uint64_t id = 0);
    ~RecvStream();

    // close the stream
    virtual void Close(uint64_t error = 0);

    // process recv frames
    virtual uint32_t OnFrame(std::shared_ptr<IFrame> frame);

    // try generate data to send
    virtual IStream::TrySendResult TrySendData(IFrameVisitor* visitor);

protected:
    uint32_t OnStreamFrame(std::shared_ptr<IFrame> frame);
    void OnStreamDataBlockFrame(std::shared_ptr<IFrame> frame);
    void OnResetStreamFrame(std::shared_ptr<IFrame> frame);

protected:
    uint64_t _final_offset;
    // peer send data limit
    uint32_t _local_data_limit;
    // next except data offset
    uint64_t _except_offset;
    std::shared_ptr<common::IBufferChains> _recv_buffer;
    std::unordered_map<uint64_t, std::shared_ptr<IFrame>> _out_order_frame;
};

}
}

#endif