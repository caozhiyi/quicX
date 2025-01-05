#ifndef QUIC_STREAM_SEND_STREAM
#define QUIC_STREAM_SEND_STREAM

#include <list>
#include <string>
#include "quic/stream/if_stream.h"
#include "common/alloter/pool_alloter.h"
#include "common/buffer/if_buffer_chains.h"
#include "quic/stream/state_machine_send.h"
#include "quic/include/if_quic_send_stream.h"

namespace quicx {
namespace quic {

class SendStream:
    public virtual IStream,
    public virtual IQuicSendStream {
public:
    SendStream(std::shared_ptr<common::BlockMemoryPool>& alloter,
        uint64_t init_data_limit,
        uint64_t id,
        std::function<void(std::shared_ptr<IStream>)> active_send_cb,
        std::function<void(uint64_t stream_id)> stream_close_cb,
        std::function<void(uint64_t error, uint16_t frame_type, const std::string& resion)> connection_close_cb);
    virtual ~SendStream();

    // *************** outside interface ***************//
    virtual StreamDirection GetDirection() { return StreamDirection::SD_SEND; }
    virtual uint64_t GetStreamID() { return stream_id_; }

    virtual void Close();

    virtual void Reset(uint32_t error);

    // send data to peer, return the number of bytes sended.
    virtual int32_t Send(uint8_t* data, uint32_t len);
    virtual int32_t Send(std::shared_ptr<common::IBufferRead> buffer);

    virtual void SetStreamWriteCallBack(stream_write_callback cb) { sended_cb_ = cb; }

    // *************** inside interface ***************//
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

    std::shared_ptr<StreamStateMachineSend> send_machine_;
    stream_write_callback sended_cb_;
};

}
}

#endif