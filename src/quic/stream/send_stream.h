#ifndef QUIC_STREAM_SEND_STREAM
#define QUIC_STREAM_SEND_STREAM

#include <map>
#include <string>

#include "common/buffer/multi_block_buffer.h"
#include <quicx/common/if_buffer_read.h>
#include <quicx/common/if_buffer_write.h>

#include <quicx/quic/if_quic_send_stream.h>
#include "quic/stream/if_stream.h"
#include "quic/stream/state_machine_send.h"

namespace quicx {
namespace quic {

class SendStream: public virtual IStream, public virtual IQuicSendStream {
public:
    SendStream(std::weak_ptr<common::IEventLoop> loop, uint64_t init_data_limit, uint64_t id,
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
    virtual IStream::TrySendResult TrySendData(IFrameVisitor* visitor, EncryptionLevel level = kApplication) override;

    // Stream data ACK tracking.
    //
    // Precise byte-range overload — preferred path. Marks the half-open
    // interval [offset_start, offset_start + length) as ACKed. The stream
    // only transitions to "Data Recvd" once *every* byte in
    // [0, send_data_offset_) is covered AND has_fin has been observed.
    virtual void OnDataAcked(uint64_t offset_start, uint64_t length, bool has_fin);

    // Backwards-compat overload kept for unit tests and call sites that only
    // know a cumulative high-water mark. Treats the call as "[0, max_offset)
    // is ACKed", which is strictly safe (cumulative ⊆ selective).
    void OnDataAcked(uint64_t max_offset, bool has_fin) { OnDataAcked(0, max_offset, has_fin); }

    // Getter for testing
    std::shared_ptr<StreamStateMachineSend> GetSendStateMachine() const { return send_machine_; }

protected:
    void OnMaxStreamDataFrame(std::shared_ptr<IFrame> frame);
    void OnStopSendingFrame(std::shared_ptr<IFrame> frame);
    void CheckAllDataAcked();

protected:
    bool to_fin_;                // whether to send fin
    uint64_t send_data_offset_;  // the offset of data that has been sent
    uint64_t acked_offset_;      // contiguous ACKed prefix length: [0, acked_offset_) fully covered
    bool fin_sent_;              // whether FIN has been sent (and ACKed if observed via callback)
    uint64_t peer_data_limit_;   // the data limit that peer limit
    uint64_t blocked_at_limit_;  // the limit at which we sent STREAM_DATA_BLOCKED (to avoid duplicate)
    std::shared_ptr<common::MultiBlockBuffer> send_buffer_;

    // Selective ACK byte ranges over the stream's send-side offset space.
    // Each entry is [start, end) — the keys are start offsets, the values are
    // (exclusive) end offsets. Entries are kept disjoint and merged on insert.
    // We must NOT degrade this to a single high-water mark: aioquic-style
    // peers can ACK a packet carrying (5MB, FIN) while still missing earlier
    // segments, and treating that as "all done" stops retransmission and
    // strands the connection until idle_timeout.
    std::map<uint64_t, uint64_t> acked_ranges_;

    std::shared_ptr<StreamStateMachineSend> send_machine_;
    stream_write_callback sended_cb_;
};

}  // namespace quic
}  // namespace quicx

#endif