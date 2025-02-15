#include "quic/stream/if_stream.h"
namespace quicx {
namespace quic {

IStream::IStream(uint64_t id,
    std::function<void(std::shared_ptr<IStream>)> active_send_cb,
    std::function<void(uint64_t stream_id)> stream_close_cb,
    std::function<void(uint64_t error, uint16_t frame_type, const std::string& resion)> connection_close_cb):
    stream_id_(id),
    active_send_cb_(active_send_cb),
    stream_close_cb_(stream_close_cb),
    connection_close_cb_(connection_close_cb),
    is_active_send_(false) {

}

IStream::~IStream() {

}

IStream::TrySendResult IStream::TrySendData(IFrameVisitor* visitor) {
    is_active_send_ = false;
    return TrySendResult::kSuccess;
}

void IStream::ToClose() {
    if (stream_close_cb_) {
        stream_close_cb_(stream_id_);
    }
}

void IStream::ToSend() {
    if (is_active_send_) {
        //return; // TODO crypto stream need resend
    }
    is_active_send_ = true;

    if (active_send_cb_) {
        active_send_cb_(shared_from_this());
    }
}

}
}
