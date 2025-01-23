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
    processors_map_.reserve(thread_num);
    for (size_t i = 0; i < thread_num; i++) {
        auto processor = std::make_shared<ProcessorClient>(tls_ctx_, params_, connection_state_cb_);
        processor->Start();
        processors_map_.emplace(processor->GetCurrentThreadId(), processor);
    }
    return true;
}

void QuicClient::Join() {
    QuicBase::Join();
}

void QuicClient::Destroy() {
    QuicBase::Destroy();
}

void QuicClient::AddTimer(uint32_t interval_ms, timer_callback cb) {
    QuicBase::AddTimer(interval_ms, cb);
}

bool QuicClient::Connection(const std::string& ip, uint16_t port,
    const std::string& alpn, int32_t timeout_ms) {
    if (!processors_map_.empty()) {
        auto iter = processors_map_.begin();
        std::advance(iter, rand() % processors_map_.size());
        auto processor = std::dynamic_pointer_cast<ProcessorClient>(iter->second);
        // if the current thread is the same as the processor's thread, then connect directly
        if (std::this_thread::get_id() == iter->first) {
           processor->Connect(ip, port, alpn, timeout_ms);

        } else {
            iter->second->Push([ip, port, alpn, timeout_ms, processor]() {
                processor->Connect(ip, port, alpn, timeout_ms);
            });
            iter->second->Weakup();
        }
        return true;
    }
    return false;
}

void QuicClient::SetConnectionStateCallBack(connection_state_callback cb) {
    QuicBase::SetConnectionStateCallBack(cb);
}

}
}