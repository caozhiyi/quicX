#ifndef QUIC_STREAM_SEND_STREAM
#define QUIC_STREAM_SEND_STREAM

#include <list>
#include <string>
#include "common/alloter/pool_alloter.h"
#include "quic/stream/send_stream_interface.h"
#include "common/buffer/buffer_chains_interface.h"

namespace quicx {

class SendStream:
    public virtual ISendStream {
public:
    SendStream(std::shared_ptr<BlockMemoryPool>& alloter, uint64_t id = 0);
    virtual ~SendStream();

    // send data to peer
    virtual int32_t Send(uint8_t* data, uint32_t len);

    // reset the stream
    virtual void Reset(uint64_t error);
 
    // close the stream
    virtual void Close(uint64_t error = 0);

    // process recv frames
    virtual void OnFrame(std::shared_ptr<IFrame> frame);

    // try generate data to send
    virtual IStream::TrySendResult TrySendData(IFrameVisitor* visitor);

protected:
    void OnMaxStreamDataFrame(std::shared_ptr<IFrame> frame);
    void OnStopSendingFrame(std::shared_ptr<IFrame> frame);

protected:
    bool _to_fin;
    uint64_t _data_offset;
    uint64_t _peer_data_limit;
    std::shared_ptr<IBufferChains> _send_buffer;
};

}

#endif
