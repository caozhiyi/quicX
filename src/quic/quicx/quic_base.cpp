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
    for (auto& processor : processors_map_) {
        processor.second->Join();
    }
}

void QuicBase::Destroy() {
    for (auto& processor : processors_map_) {
        processor.second->Stop();
    }
}

void QuicBase::SetConnectionStateCallBack(connection_state_callback cb) {
    connection_state_cb_ = cb;
}

void QuicBase::AddTimer(uint32_t interval_ms, timer_callback cb) {
    auto iter = processors_map_.find(std::this_thread::get_id());
    if (iter != processors_map_.end()) {
        iter->second->AddTimer(interval_ms, cb);
        return;
    }
    common::LOG_ERROR("no processor found for thread id: {}", std::this_thread::get_id());
}

void QuicBase::InitLogger(LogLevel level) {
    std::shared_ptr<common::Logger> log = std::make_shared<common::StdoutLogger>();
    common::LOG_SET(log);
    common::LOG_SET_LEVEL(common::LogLevel(level));
}

}
}