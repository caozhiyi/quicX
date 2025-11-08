#ifndef QUIC_STREAM_BIDIRECTION_STREAM
#define QUIC_STREAM_BIDIRECTION_STREAM

#include "quic/stream/recv_stream.h"
#include "quic/stream/send_stream.h"
#include "quic/include/if_quic_bidirection_stream.h"

namespace quicx {
namespace quic {

class BidirectionStream:
    public virtual SendStream,
    public virtual RecvStream,
    public virtual IQuicBidirectionStream {
public:
    BidirectionStream(std::shared_ptr<common::BlockMemoryPool> alloter,
        uint64_t init_data_limit, 
        uint64_t id,
        std::function<void(std::shared_ptr<IStream>)> active_send_cb,
        std::function<void(uint64_t stream_id)> stream_close_cb,
        std::function<void(uint64_t error, uint16_t frame_type, const std::string& resion)> connection_close_cb);
    virtual ~BidirectionStream();

    // *************** outside interface ***************//
    virtual StreamDirection GetDirection() override { return StreamDirection::kBidi; }
    virtual uint64_t GetStreamID() override { return stream_id_; }

    // close the stream gracefully, the stream will be closed after all data transported.
    virtual void Close() override;

    // close the stream immediately, the stream will be closed immediately even if there are some data inflight.
    // error code will be sent to the peer.
    virtual void Reset(uint32_t error) override;

    // send data to peer, return the number of bytes sended.
    virtual int32_t Send(uint8_t* data, uint32_t len) override;
    virtual int32_t Send(std::shared_ptr<common::IBufferRead> buffer) override;

    // called when data is ready to send, that means the data is in the send buffer.
    // called in the send thread, so do not do any blocking operation.
    // if you don't care about send data detail, you may not set the callback.
    virtual void SetStreamWriteCallBack(stream_write_callback cb) override;

    // when there are some data received, the callback function will be called.
    // the callback function will be called in the recv thread. so you should not do any blocking operation in the callback function.
    // you should set the callback function firstly, otherwise the data received will be discarded.
    virtual void SetStreamReadCallBack(stream_read_callback cb) override;

    // ***************  inner interface ***************//
    virtual uint32_t OnFrame(std::shared_ptr<IFrame> frame) override;

    virtual IStream::TrySendResult TrySendData(IFrameVisitor* visitor) override;
    
    // Override to trigger CheckStreamClose after ACK
    virtual void OnDataAcked(uint64_t max_offset, bool has_fin) override;

protected:
    void CheckStreamClose();
};

}
}

#endif