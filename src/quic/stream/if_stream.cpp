#include "quic/stream/if_stream.h"
namespace quicx {
namespace quic {

IStream::TrySendResult IStream::TrySendData(IFrameVisitor* visitor) {
    is_active_send_ = false;
    return TSR_SUCCESS;
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
