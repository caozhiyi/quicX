#ifndef QUIC_STREAM_RECV_STREAM
#define QUIC_STREAM_RECV_STREAM

#include <string>
#include <functional>
#include <unordered_map>
#include "quic/stream/if_stream.h"
#include "quic/stream/if_frame_visitor.h"
#include "quic/stream/state_machine_recv.h"
#include "quic/include/if_quic_recv_stream.h"
#include "common/buffer/multi_block_buffer.h"

namespace quicx {
namespace quic {

class RecvStream:
    public virtual IStream,
    public virtual IQuicRecvStream {
public:
    RecvStream(std::shared_ptr<common::BlockMemoryPool>& alloter,
        std::shared_ptr<common::IEventLoop> loop,
        uint64_t init_data_limit,
        uint64_t id,
        std::function<void(std::shared_ptr<IStream>)> active_send_cb,
        std::function<void(uint64_t stream_id)> stream_close_cb,
        std::function<void(uint64_t error, uint16_t frame_type, const std::string& resion)> connection_close_cb);
    ~RecvStream();

    // *************** outside interface ***************//
    virtual StreamDirection GetDirection() { return StreamDirection::kRecv; }
    virtual uint64_t GetStreamID() { return stream_id_; }
    virtual void Reset(uint32_t error);
    virtual void SetStreamReadCallBack(stream_read_callback cb) { recv_cb_ = cb; }

    // *************** inner interface ***************//
    // process recv frames
    virtual uint32_t OnFrame(std::shared_ptr<IFrame> frame);

    // try generate data to send
    virtual IStream::TrySendResult TrySendData(IFrameVisitor* visitor);
    
    // Getter for testing
    std::shared_ptr<StreamStateMachineRecv> GetRecvStateMachine() const { return recv_machine_; }

protected:
    virtual uint32_t OnStreamFrame(std::shared_ptr<IFrame> frame);
    virtual void OnStreamDataBlockFrame(std::shared_ptr<IFrame> frame);
    virtual void OnResetStreamFrame(std::shared_ptr<IFrame> frame);

protected:
    uint64_t final_offset_;
    // peer send data limit
    uint32_t local_data_limit_;
    // next except data offset
    uint64_t except_offset_;
    std::shared_ptr<common::MultiBlockBuffer> buffer_;
    std::unordered_map<uint64_t, std::shared_ptr<IFrame>> out_order_frame_;

    std::shared_ptr<StreamStateMachineRecv> recv_machine_;
    stream_read_callback recv_cb_;
};

}
}

#endif