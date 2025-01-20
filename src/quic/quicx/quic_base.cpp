#include "common/log/log.h"
#include "quic/quicx/quic_base.h"
#include "common/log/stdout_logger.h"

namespace quicx {
namespace quic {

QuicBase::QuicBase(const QuicTransportParams& params):
    params_(params) {

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


void QuicBase::InitLogger(LogLevel level) {
    std::shared_ptr<common::Logger> log = std::make_shared<common::StdoutLogger>();
    common::LOG_SET(log);
    common::LOG_SET_LEVEL(common::LogLevel(level));
}

}
}