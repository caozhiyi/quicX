#ifndef QUIC_STREAM_RECV_STREAM
#define QUIC_STREAM_RECV_STREAM

#include <list>
#include <string>
#include <functional>
#include <unordered_map>
#include "quic/stream/recv_stream_interface.h"
#include "quic/stream/frame_visitor_interface.h"

namespace quicx {

class RecvStream:
    public virtual IRecvStream {
public:
    RecvStream(std::shared_ptr<BlockMemoryPool>& alloter, uint64_t id = 0);
    ~RecvStream();

    // abort reading
    void Close();

    bool TrySendData(IFrameVisitor* visitor);

    void OnFrame(std::shared_ptr<IFrame> frame);

protected:
    void OnStreamFrame(std::shared_ptr<IFrame> frame);
    void OnStreamDataBlockFrame(std::shared_ptr<IFrame> frame);
    void OnResetStreamFrame(std::shared_ptr<IFrame> frame);

protected:
    uint64_t _final_offset;
    uint32_t _local_data_limit;  // peer send data limit

    uint64_t _except_offset;
    std::shared_ptr<IBufferChains> _recv_buffer;
    std::unordered_map<uint64_t, std::shared_ptr<IFrame>> _out_order_frame;
};

}

#endif