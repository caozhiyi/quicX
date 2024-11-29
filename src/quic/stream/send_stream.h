#ifndef QUIC_STREAM_SEND_STREAM
#define QUIC_STREAM_SEND_STREAM

#include <list>
#include <string>
#include "quic/stream/if_send_stream.h"
#include "common/alloter/pool_alloter.h"
#include "common/buffer/buffer_chains_interface.h"

namespace quicx {
namespace quic {

class SendStream:
    public virtual ISendStream {
public:
    SendStream(std::shared_ptr<common::BlockMemoryPool>& alloter, uint64_t init_data_limit, uint64_t id = 0);
    virtual ~SendStream();

    // send data to peer
    virtual int32_t Send(uint8_t* data, uint32_t len);

    // reset the stream
    virtual void Reset(uint64_t error);
 
    // close the stream
    virtual void Close();

    // process recv frames
    virtual uint32_t OnFrame(std::shared_ptr<IFrame> frame);

    // try generate data to send
    virtual IStream::TrySendResult TrySendData(IFrameVisitor* visitor);

protected:
    void OnMaxStreamDataFrame(std::shared_ptr<IFrame> frame);
    void OnStopSendingFrame(std::shared_ptr<IFrame> frame);

protected:
    bool to_fin_; // whether to send fin
    uint64_t send_data_offset_; // the offset of data that has been sent
    uint64_t peer_data_limit_;  // the data limit that peer limit
    std::shared_ptr<common::IBufferChains> send_buffer_;
};

}
}

#endif
