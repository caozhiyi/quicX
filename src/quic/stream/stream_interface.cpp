#include "quic/stream/stream_interface.h"
namespace quicx {

IStream::TrySendResult IStream::TrySendData(IFrameVisitor* visitor) {
    _is_active_send = false;
    return TSR_SUCCESS;
}

void IStream::ActiveToSend() {
    if (_is_active_send) {
        //return;
    }
    _is_active_send = true;

    if (_active_send_cb) {
        _active_send_cb(this);
    }
}

}
