#include "common/log/log.h"
#include "quic/quicx/quic_client.h"
#include "quic/quicx/processor_client.h"

namespace quicx {
namespace quic {

std::shared_ptr<IQuicClient> IQuicClient::Create(const QuicTransportParams& params) {
    return std::make_shared<QuicClient>(params);
}

QuicClient::QuicClient(const QuicTransportParams& params):
    QuicBase(params) {

}

QuicClient::~QuicClient() {

}

bool QuicClient::Init(uint16_t thread_num, LogLevel level) {
    if (level != LL_NULL) {
        InitLogger(level);
    }
    tls_ctx_ = std::make_shared<TLSClientCtx>();
    if (!tls_ctx_->Init()) {
        common::LOG_ERROR("tls ctx init faliled.");
        return false;
    }
    processors_.reserve(thread_num);
    for (size_t i = 0; i < thread_num; i++) {
        auto processor = std::make_shared<ProcessorClient>(tls_ctx_, params_, connection_state_cb_);
        processor->Start();
        processors_.emplace_back(processor);
    }
    return true;
}

void QuicClient::Join() {
    QuicBase::Join();
}

void QuicClient::Destroy() {
    QuicBase::Destroy();
}

bool QuicClient::Connection(const std::string& ip, uint16_t port,
    const std::string& alpn, int32_t timeout_ms) {
    if (!processors_.empty()) {
        // TODO: random select processor
        std::dynamic_pointer_cast<ProcessorClient>(processors_[0])->Connect(ip, port, alpn, timeout_ms);
        return true;
    }
    return false;
}

void QuicClient::SetConnectionStateCallBack(connection_state_callback cb) {
    QuicBase::SetConnectionStateCallBack(cb);
}

}
}