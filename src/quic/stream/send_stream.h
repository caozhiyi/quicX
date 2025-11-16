#ifndef QUIC_STREAM_SEND_STREAM
#define QUIC_STREAM_SEND_STREAM

#include <string>
#include "quic/stream/if_stream.h"
#include "common/include/if_buffer_read.h"
#include "common/include/if_buffer_write.h"
#include "quic/stream/state_machine_send.h"
#include "quic/include/if_quic_send_stream.h"
#include "common/buffer/multi_block_buffer.h"

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
    virtual StreamDirection GetDirection() override { return StreamDirection::kSend; }
    virtual uint64_t GetStreamID() override { return stream_id_; }

    virtual void Close() override;

    virtual void Reset(uint32_t error) override;

    // send data to peer, return the number of bytes sended.
    virtual int32_t Send(uint8_t* data, uint32_t len) override;
    virtual int32_t Send(std::shared_ptr<IBufferRead> buffer) override;
    virtual std::shared_ptr<IBufferWrite> GetSendBuffer() override;
    virtual bool Flush() override;

    virtual void SetStreamWriteCallBack(stream_write_callback cb) override { sended_cb_ = cb; }

    // *************** inside interface ***************//
    // process recv frames
    virtual uint32_t OnFrame(std::shared_ptr<IFrame> frame) override;

    // try generate data to send
    virtual IStream::TrySendResult TrySendData(IFrameVisitor* visitor) override;
    
    // Stream data ACK tracking
    virtual void OnDataAcked(uint64_t max_offset, bool has_fin);
    
    // Getter for testing
    std::shared_ptr<StreamStateMachineSend> GetSendStateMachine() const { return send_machine_; }

protected:
    void OnMaxStreamDataFrame(std::shared_ptr<IFrame> frame);
    void OnStopSendingFrame(std::shared_ptr<IFrame> frame);
    void CheckAllDataAcked();

protected:
    bool to_fin_; // whether to send fin
    uint64_t send_data_offset_; // the offset of data that has been sent
    uint64_t acked_offset_;     // the maximum offset that has been ACKed
    bool fin_sent_;             // whether FIN has been sent
    uint64_t peer_data_limit_;  // the data limit that peer limit
    std::shared_ptr<common::MultiBlockBuffer> send_buffer_;

    std::shared_ptr<StreamStateMachineSend> send_machine_;
    stream_write_callback sended_cb_;
};

}
}

#endif