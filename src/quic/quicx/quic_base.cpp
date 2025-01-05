#include "common/log/log.h"
#include "quic/quicx/quic_base.h"
#include "quic/quicx/thread_processor.h"

namespace quicx {
namespace quic {

QuicBase::QuicBase() {

}

QuicBase::~QuicBase() {

}

bool QuicBase::Init(uint16_t thread_num) {
    processors_.resize(thread_num);
    for (size_t i = 0; i < thread_num; i++) {
        auto processor = std::make_shared<ThreadProcessor>(tls_ctx_, connection_state_cb_);
        processor->Start();
        processors_.emplace_back(processor);
    }
    return true;
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