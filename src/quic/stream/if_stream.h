#ifndef QUIC_STREAM_IF_STREAM
#define QUIC_STREAM_IF_STREAM

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <quicx/quic/if_quic_stream.h>
#include <string>

#include "common/network/if_event_loop.h"

#include "quic/crypto/tls/type.h"
#include "quic/frame/if_frame.h"
#include "quic/stream/if_frame_visitor.h"

namespace quicx {
namespace quic {

class IStream: public virtual IQuicStream, public std::enable_shared_from_this<IStream> {
public:
    IStream(std::weak_ptr<common::IEventLoop> loop, uint64_t stream_id,
        std::function<void(std::shared_ptr<IStream>)> active_send_cb,
        std::function<void(uint64_t stream_id)> stream_close_cb,
        std::function<void(uint64_t error, uint16_t frame_type, const std::string& resion)> connection_close_cb):
        stream_id_(stream_id),
        stream_close_cb_(stream_close_cb),
        active_send_cb_(active_send_cb),
        connection_close_cb_(connection_close_cb),
        event_loop_(loop) {}
    virtual ~IStream();
    // process recv frames
    // return stream data size
    virtual uint32_t OnFrame(std::shared_ptr<IFrame> frame) = 0;

    // try generate data to send
    enum class TrySendResult {
        kSuccess = 0,             // generate data done
        kFailed = 1,              // generate data failed (permanent error, remove from active list)
        kBreak = 2,               // generate data need send alone (packet full)
        kFlowControlBlocked = 3,  // blocked by flow control, keep in active list waiting for window update
    };

    virtual TrySendResult TrySendData(IFrameVisitor* visitor, EncryptionLevel level = kApplication);

protected:
    void ToClose();
    void ToSend();

    // P0-C10 / INV1: give a stream-level control frame an owner. With the
    // whole-packet retransmission channel retired, a frame that leaves in a
    // lost packet is only ever re-sent if SOMEONE re-queues it. These frames
    // (RESET_STREAM, STOP_SENDING, MAX_STREAM_DATA, STREAM_DATA_BLOCKED) are
    // written straight into the packet by the stream's own TrySendData, so
    // the stream is the natural owner: on kLost/kProbe the SAME frame object
    // goes back into frames_list_ (idempotent state, safe to repeat) and the
    // stream is re-activated. `should_resend` lets a caller veto a resend that
    // has become pointless (e.g. a window update superseded by a larger one).
    void TrackControlFrame(const std::shared_ptr<IFrame>& frame, std::function<bool()> should_resend = nullptr);

protected:
    void* user_data_ = nullptr;

    uint64_t stream_id_;
    // is already active to send?
    bool is_active_send_ = false;
    // frames that wait for sending
    std::list<std::shared_ptr<IFrame>> frames_list_;

    // stream close call back
    std::function<void(uint64_t stream_id)> stream_close_cb_;
    // put stream to active send list callback
    std::function<void(std::shared_ptr<IStream> stream)> active_send_cb_;
    // inner connection close callback
    std::function<void(uint64_t error, uint16_t frame_type, const std::string& resion)> connection_close_cb_;

    std::weak_ptr<common::IEventLoop> event_loop_;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_STREAM_IF_STREAM
