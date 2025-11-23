#include "quic/stream/if_stream.h"
#include "quic/connection/controler/send_control.h"

namespace quicx {
namespace quic {

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
