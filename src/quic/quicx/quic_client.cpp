#include "quic/quicx/quic_client.h"

namespace quicx {
namespace quic {

std::shared_ptr<IQuicClient> IQuicClient::Create(const QuicTransportParams& params) {
    return std::make_shared<QuicClient>(params);
}

QuicClient::QuicClient(const QuicTransportParams& params):
    Quic(params) {
    master_ = IMaster::MakeMaster();
}

QuicClient::~QuicClient() {

}

bool QuicClient::Init(const QuicConfig& config) {
    if (config.log_level_ != LogLevel::kNull) {
        InitLogger(config.log_level_);
    }
    
    master_->InitAsClient(config, params_, connection_state_cb_);
    return true;
}

void QuicClient::Join() {
    Quic::Join();
}

void QuicClient::Destroy() {
    Quic::Destroy();
}

void QuicClient::AddTimer(uint32_t timeout_ms, std::function<void()> cb) {
    master_->AddTimer(timeout_ms, cb);
}

bool QuicClient::Connection(const std::string& ip, uint16_t port,
    const std::string& alpn, int32_t timeout_ms) {
    return master_->Connection(ip, port, alpn, timeout_ms);
}

bool QuicClient::Connection(const std::string& ip, uint16_t port,
    const std::string& alpn, int32_t timeout_ms, const std::string& resumption_session_der) {
    return master_->Connection(ip, port, alpn, timeout_ms, resumption_session_der);
}

void QuicClient::SetConnectionStateCallBack(connection_state_callback cb) {
    Quic::SetConnectionStateCallBack(cb);
}

}
}