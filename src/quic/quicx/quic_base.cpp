#include "common/log/log.h"
#include "quic/quicx/quic_base.h"

namespace quicx {
namespace quic {

QuicBase::QuicBase() {

}

QuicBase::~QuicBase() {

}

void QuicBase::Join() {
    for (auto& processor : processors_) {
        processor->Join();
    }
}

void QuicBase::Destroy() {
    for (auto& processor : processors_) {
        processor->Stop();
    }
}

void QuicBase::SetConnectionStateCallBack(connection_state_callback cb) {
    connection_state_cb_ = cb;
}

}
}