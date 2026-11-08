#ifndef QUIC_STREAM_SEND_STREAM
#define QUIC_STREAM_SEND_STREAM

#include <map>
#include <quicx/common/if_buffer_read.h>
#include <quicx/common/if_buffer_write.h>
#include <quicx/quic/if_quic_send_stream.h>
#include <string>
#include <utility>
#include <vector>

#include "common/buffer/multi_block_buffer.h"
#include "common/buffer/shared_buffer_span.h"
#include "quic/frame/stream_frame.h"

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

    virtual uint64_t GetPendingSendBytes() override { return send_buffer_ ? send_buffer_->GetDataLength() : 0; }

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

    // P0-C10 / D2: frame-level loss (kLost) and probe (kProbe) notification
    // for a STREAM frame this stream emitted. Re-queues the sub-ranges of
    // [offset, offset + data.length) that are neither ACKed nor already queued
    // (the CryptoStream "uncovered gaps" algorithm), carrying the FIN bit
    // forward when the lost frame had it. `data` is the lost frame's own
    // zero-copy span, so no retained copy of the send buffer is needed.
    void OnFrameLost(uint64_t offset, const common::SharedBufferSpan& data, bool fin);

    // True while lost/probed STREAM bytes (or a lost FIN) await re-emission.
    bool HasPendingRetransmit() const { return !retx_queue_.empty(); }
    size_t GetPendingRetransmitSegments() const { return retx_queue_.size(); }

protected:
    void OnMaxStreamDataFrame(std::shared_ptr<IFrame> frame);
    void OnStopSendingFrame(std::shared_ptr<IFrame> frame);
    void CheckAllDataAcked();

    // Attach the delivery handler that routes kLost/kProbe of `frame` to
    // OnFrameLost. kAcked is deliberately ignored here: ACKs reach
    // OnDataAcked through the StreamDataInfo path (precise byte ranges).
    void AttachLossHandler(const std::shared_ptr<StreamFrame>& frame);

    // Emit queued retransmissions into the current packet. Bypasses the
    // peer-flow-control and send-state gates (the bytes were already admitted
    // once) and never calls visitor->AddStreamDataSize (connection-level
    // flow control must not be charged twice for the same bytes).
    TrySendResult DrainRetransmit(IFrameVisitor* visitor);

    // Sub-ranges of [start, end) not covered by acked_ranges_ or retx_queue_.
    std::vector<std::pair<uint64_t, uint64_t>> UncoveredGaps(uint64_t start, uint64_t end) const;

protected:
    bool to_fin_;                // whether to send fin
    uint64_t send_data_offset_;  // the offset of data that has been sent
    uint64_t acked_offset_;      // contiguous ACKed prefix length: [0, acked_offset_) fully covered
    bool fin_sent_;              // whether FIN has been sent (and ACKed if observed via callback)
    bool fin_acked_;             // whether a STREAM frame carrying FIN was acknowledged

    // Lost/probed STREAM ranges waiting for re-emission, keyed by start
    // offset, pairwise disjoint. `data` begins exactly at the key; a segment
    // with end == key is a FIN-only retransmission.
    struct RetxSegment {
        uint64_t end;
        common::SharedBufferSpan data;
        bool fin;
    };
    std::map<uint64_t, RetxSegment> retx_queue_;
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

#endif  // QUIC_STREAM_SEND_STREAM