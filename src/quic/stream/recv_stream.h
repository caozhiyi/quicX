#ifndef QUIC_STREAM_RECV_STREAM
#define QUIC_STREAM_RECV_STREAM

#include <functional>
#include <map>
#include <quicx/quic/if_quic_recv_stream.h>
#include <string>
#include <unordered_map>

#include "common/buffer/multi_block_buffer.h"

#include "quic/stream/if_frame_visitor.h"
#include "quic/stream/if_stream.h"
#include "quic/stream/state_machine_recv.h"

namespace quicx {
namespace quic {

class RecvStream: public virtual IStream, public virtual IQuicRecvStream {
public:
    RecvStream(std::weak_ptr<common::IEventLoop> loop, uint64_t init_data_limit, uint64_t id,
        std::function<void(std::shared_ptr<IStream>)> active_send_cb,
        std::function<void(uint64_t stream_id)> stream_close_cb,
        std::function<void(uint64_t error, uint16_t frame_type, const std::string& resion)> connection_close_cb);
    ~RecvStream();

    // *************** outside interface ***************//
    virtual StreamDirection GetDirection() { return StreamDirection::kRecv; }
    virtual uint64_t GetStreamID() { return stream_id_; }
    virtual void Reset(uint32_t error);
    // Installs the read callback. If data already arrived before the callback
    // was attached (the application layer can be wired up later than the
    // transport, e.g. HTTP/3 is only built after the handshake completes while
    // the peer's unidirectional streams arrive in the same flight), the
    // buffered bytes are delivered immediately instead of being stranded.
    virtual void SetStreamReadCallBack(stream_read_callback cb);

    // *************** inner interface ***************//
    // process recv frames, return the number of bytes consumed.
    virtual uint32_t OnFrame(std::shared_ptr<IFrame> frame);

    // try generate data to send
    virtual IStream::TrySendResult TrySendData(IFrameVisitor* visitor);

    // P0-S2: total bytes this stream currently pins in receive memory
    // (ordered buffer + out-of-order reassembly). Consumed by the
    // connection-level receive budget via StreamManager.
    uint64_t GetBufferedBytes() const {
        return out_order_bytes_ + (buffer_ ? buffer_->GetDataLength() : 0);
    }
    // P0-S2: report receive-memory deltas to the owner (StreamManager feeds
    // the connection-level budget). Invoked from the accounting snapshot
    // points: frame arrival, application consumption, stream teardown.
    void SetBufferedChangeCallback(std::function<void(int64_t delta)> cb) { buffered_change_cb_ = std::move(cb); }

    // Getter for testing
    std::shared_ptr<StreamStateMachineRecv> GetRecvStateMachine() const { return recv_machine_; }

protected:
    // Hands data that arrived before a read callback was installed over to the
    // now-registered callback. Always invoked from the event loop.
    void FlushBufferedData();

    virtual uint32_t OnStreamFrame(std::shared_ptr<IFrame> frame);
    virtual void OnStreamDataBlockFrame(std::shared_ptr<IFrame> frame);
    virtual void OnResetStreamFrame(std::shared_ptr<IFrame> frame);

    // P0-S2: consumption-driven MAX_STREAM_DATA. The window only grows when
    // the APPLICATION has drained past it — bytes sitting in the buffer keep
    // their share of the window occupied. Called after every recv callback
    // return and on STREAM_DATA_BLOCKED (which no longer raises the window
    // by itself).
    void MaybeSendWindowUpdate();
    // Snapshot-style accounting of pinned receive bytes; reports the delta
    // against the last reported value to buffered_change_cb_.
    void UpdateBufferedAccounting();

    // Invoked after receive-side state transitions that may have reached a
    // terminal state (Data Read / Reset Read, RFC 9000 §3.2). A standalone
    // receive-only stream closes entirely there; BidirectionStream overrides
    // this to require BOTH directions terminal (RFC 9000 §3.5).
    virtual void CheckStreamClose();

protected:
    uint64_t final_offset_;
    // Tracks whether a final offset (FIN / final size) has been received. A separate bool
    // is required because final_offset_ == 0 is a valid value (a zero-length stream
    // finalized at offset 0), which the old `final_offset_ != 0` sentinel could not
    // distinguish from "no final offset yet".
    bool has_final_offset_ = false;
    // Connection-level flow-control accounting anchor (RFC 9000 §4.5/A.2):
    // the highest stream offset (or final size, once known) whose bytes have
    // already been counted toward the connection window. OnStreamFrame returns
    // only the delta above this mark, so reordered/re-segmented retransmissions
    // of already-counted bytes are never double-counted. Always >= except_offset_.
    uint64_t fc_counted_offset_ = 0;
    // peer send data limit
    uint64_t local_data_limit_;
    // next except data offset
    uint64_t except_offset_;
    std::shared_ptr<common::MultiBlockBuffer> buffer_;
    // Ordered by offset: the reassembly drain loop relies on begin() being the
    // lowest buffered offset (prefix-trim + absorb until the first real gap).
    std::map<uint64_t, std::shared_ptr<IFrame>> out_order_frame_;
    // Running total of bytes currently held in out_order_frame_. It only ever
    // decreases when frames are consumed in order (the drain loop); we never
    // evict buffered frames, because out-of-order frames are already ACKed at the
    // packet level and would be lost permanently if dropped (the sender will not
    // retransmit ACKed data), stalling the stream.
    uint64_t out_order_bytes_ = 0;

    std::shared_ptr<StreamStateMachineRecv> recv_machine_;
    stream_read_callback recv_cb_;
    uint32_t reset_error_;

    // True while a catch-up delivery is queued on the event loop, so repeated
    // SetStreamReadCallBack() calls (the HTTP/3 stream-type handover installs a
    // callback several times) do not pile up duplicate tasks.
    bool flush_pending_data_posted_ = false;
    // P0-S3: out-of-order frames smaller than this are deep-copied into a
    // standalone chunk before buffering. Without the copy a 1-byte frame
    // arriving out of order pins its whole ~1.5KB packet chunk (and larger
    // spans pin larger blocks); a peer spraying tiny frames behind a gap
    // multiplied real memory use by the chunk-to-frame size ratio. Frames
    // occupying a meaningful share of their chunk keep the zero-copy span —
    // the copy would cost about as much as it saves.
    static constexpr uint32_t kOutOfOrderCopyThreshold = 2048;
    static constexpr uint32_t kOutOfOrderCopyMinFactor = 4;  // copy only when chunk > 4x frame
    // Last GetBufferedBytes() value reported through buffered_change_cb_.
    // The snapshot diff absorbs both directions (arrival grows it, app
    // consumption / drain-loop absorption shrinks it) without needing a
    // callback at every buffer operation.
    uint64_t last_reported_buffered_ = 0;
    std::function<void(int64_t delta)> buffered_change_cb_;
    // P0-S10: local STOP_SENDING was sent (application stopped reading with
    // an error). Later in-flight STREAM data is flow-control-accounted then
    // discarded; the receive state machine is NOT advanced — the real
    // RESET_STREAM from the peer is what terminates the stream, so it must
    // stay processable (the old code faked a kResetRecvd transition here,
    // which made the peer's genuine RESET_STREAM unprocessable and leaked
    // the stream).
    bool local_stop_sending_ = false;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_STREAM_RECV_STREAM