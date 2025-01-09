#include "common/log/log.h"
#include "quic/quicx/quic_client.h"

namespace quicx {
namespace quic {

std::shared_ptr<IQuicClient> IQuicClient::Create() {
    return std::make_shared<QuicClient>();
}

QuicClient::QuicClient() {

}

QuicClient::~QuicClient() {

}

bool QuicClient::Init(uint16_t thread_num) {
    tls_ctx_ = std::make_shared<TLSClientCtx>();
    if (!tls_ctx_->Init()) {
        common::LOG_ERROR("tls ctx init faliled.");
        return false;
    }
    return QuicBase::Init(thread_num);
}

void QuicClient::Join() {
    QuicBase::Join();
}

void QuicClient::Destroy() {
    QuicBase::Destroy();
}

bool QuicClient::Connection(const std::string& ip, uint16_t port, int32_t timeout_ms) {
    if (!processors_.empty()) {
        // TODO: random select processor
        processors_[0]->Connect(ip, port);
        return true;
    }
    return false;
}

void QuicClient::SetConnectionStateCallBack(connection_state_callback cb) {
    QuicBase::SetConnectionStateCallBack(cb);
}

}
}