#include "quic/stream/if_stream.h"

namespace quicx {
namespace quic {

IStream::~IStream() {}

IStream::TrySendResult IStream::TrySendData(IFrameVisitor* /*visitor*/, EncryptionLevel /*level*/) {
    is_active_send_ = false;
    return TrySendResult::kSuccess;
}

void IStream::ToClose() {
    if (stream_close_cb_) {
        stream_close_cb_(stream_id_);
    }
}

void IStream::TrackControlFrame(const std::shared_ptr<IFrame>& frame, std::function<bool()> should_resend) {
    // weak_from_this: the frame lives in the unacked table long after the
    // stream may be gone; the handler must neither keep it alive nor touch it.
    // The frame is captured weakly too (the handler lives ON the frame).
    std::weak_ptr<IStream> weak_stream = weak_from_this();
    std::weak_ptr<IFrame> weak_frame = frame;
    frame->SetDeliveryHandler([weak_stream, weak_frame, should_resend = std::move(should_resend)](
                                  FrameDeliveryState state) {
        if (state == FrameDeliveryState::kAcked) {
            return;
        }
        auto stream = weak_stream.lock();
        auto f = weak_frame.lock();
        if (!stream || !f) {
            return;
        }
        if (should_resend && !should_resend()) {
            return;
        }
        for (const auto& queued : stream->frames_list_) {
            if (queued == f) {
                return;  // already waiting; one copy is enough
            }
        }
        f->RearmLossNotification();
        stream->frames_list_.emplace_back(f);
        stream->ToSend();
    });
}

void IStream::ToSend() {
    // Always (re)arm the active-send flag and notify the connection's send
    // scheduler. We deliberately do not early-return when is_active_send_ is
    // already true: the crypto stream relies on retriggering ToSend() to
    // re-emit CRYPTO frames during handshake retransmission, and bidi/uni
    // streams in active state may also have new bytes since the last notify.
    is_active_send_ = true;

    if (active_send_cb_) {
        active_send_cb_(shared_from_this());
    }
}

}  // namespace quic
}  // namespace quicx
