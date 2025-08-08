#include "common/log/log.h"
#include "quic/quicx/quic.h"
#include "common/log/stdout_logger.h"

namespace quicx {
namespace quic {

Quic::Quic(const QuicTransportParams& params):
    params_(params) {
    master_ = IMaster::MakeMaster();
}

Quic::~Quic() {

}

void Quic::Join() {
    master_->Join();
}

void Quic::Destroy() {
    master_->Destroy();
}

void Quic::SetConnectionStateCallBack(connection_state_callback cb) {
    connection_state_cb_ = cb;
}

void Quic::InitLogger(LogLevel level) {
    std::shared_ptr<common::Logger> log = std::make_shared<common::StdoutLogger>();
    common::LOG_SET(log);
    common::LOG_SET_LEVEL(common::LogLevel(level));
}

}
}