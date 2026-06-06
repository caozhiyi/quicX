#include "quic/stream/if_stream.h"

namespace quicx {
namespace quic {

IStream::~IStream() {}

IStream::TrySendResult IStream::TrySendData(IFrameVisitor* visitor, EncryptionLevel level) {
    is_active_send_ = false;
    return TrySendResult::kSuccess;
}

void IStream::ToClose() {
    if (stream_close_cb_) {
        stream_close_cb_(stream_id_);
    }
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
