#ifndef QUIC_STREAM_BIDIRECTION_STREAM
#define QUIC_STREAM_BIDIRECTION_STREAM

#include "quic/stream/recv_stream.h"
#include "quic/stream/send_stream.h"

namespace quicx {

class BidirectionStream:
    public ISendStream,
    public IRecvStream {
public:
    BidirectionStream(std::shared_ptr<BlockMemoryPool> alloter, uint64_t id = 0);
    virtual ~BidirectionStream();

    virtual void Close();

    virtual void OnFrame(std::shared_ptr<IFrame> frame);

    virtual bool TrySendData(IFrameVisitor* visitor);

    virtual int32_t Send(uint8_t* data, uint32_t len);

    // reset the stream
    virtual void Reset(uint64_t err);

protected:
    uint64_t _final_offset;
    uint64_t _data_offset;
    uint64_t _peer_data_limit;
    uint64_t _local_data_limit;

    std::shared_ptr<BlockMemoryPool> _alloter;
    std::shared_ptr<IBufferChains> _recv_buffer;
    std::shared_ptr<IBufferChains> _send_buffer;

    std::list<std::shared_ptr<IFrame>> _frame_list;
};

}

#endif