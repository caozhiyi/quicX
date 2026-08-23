#ifndef QUIC_STREAM_RECV_STREAM
#define QUIC_STREAM_RECV_STREAM

#include <functional>
#include <map>
#include <string>
#include <unordered_map>

#include "common/buffer/multi_block_buffer.h"

#include <quicx/quic/if_quic_recv_stream.h>
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

    // Getter for testing
    std::shared_ptr<StreamStateMachineRecv> GetRecvStateMachine() const { return recv_machine_; }

protected:
    // Hands data that arrived before a read callback was installed over to the
    // now-registered callback. Always invoked from the event loop.
    void FlushBufferedData();

    virtual uint32_t OnStreamFrame(std::shared_ptr<IFrame> frame);
    virtual void OnStreamDataBlockFrame(std::shared_ptr<IFrame> frame);
    virtual void OnResetStreamFrame(std::shared_ptr<IFrame> frame);

protected:
    uint64_t final_offset_;
    // Tracks whether a final offset (FIN / final size) has been received. A separate bool
    // is required because final_offset_ == 0 is a valid value (a zero-length stream
    // finalized at offset 0), which the old `final_offset_ != 0` sentinel could not
    // distinguish from "no final offset yet".
    bool has_final_offset_ = false;
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
};

}  // namespace quic
}  // namespace quicx

#endif