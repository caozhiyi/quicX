#ifndef QUIC_STREAM_SEND_STREAM
#define QUIC_STREAM_SEND_STREAM

#include <list>
#include <string>
#include "common/alloter/pool_alloter.h"
#include "quic/stream/send_stream_interface.h"
#include "common/buffer/buffer_chains_interface.h"

namespace quicx {

class SendStream:
    public ISendStream {
public:
    SendStream(std::shared_ptr<BlockMemoryPool>& alloter, uint64_t id = 0);
    virtual ~SendStream();

    virtual int32_t Send(uint8_t* data, uint32_t len);

    // reset the stream
    virtual void Reset(uint64_t err);
 
    // end the stream
    virtual void Close();

    virtual void OnFrame(std::shared_ptr<IFrame> frame);

    virtual bool TrySendData(SendDataVisitor& visitior);

private:
    void OnMaxStreamDataFrame(std::shared_ptr<IFrame> frame);
    void OnStopSendingFrame(std::shared_ptr<IFrame> frame);

private:
    uint64_t _data_offset;
    uint64_t _peer_data_limit;
    std::shared_ptr<IBufferChains> _send_buffer;
    std::list<std::shared_ptr<IFrame>> _frame_list;
};

}

#endif
