#include "quic/quicx/quic_server.h"

namespace quicx {
namespace quic {

std::shared_ptr<IQuicServer> IQuicServer::Create(const QuicTransportParams& params) {
    return std::make_shared<QuicServer>(params);
}

QuicServer::QuicServer(const QuicTransportParams& params):
    Quic(params) {
    master_ = IMaster::MakeMaster();
}

QuicServer::~QuicServer() {

}

bool QuicServer::Init(const QuicServerConfig& config) {
    if (config.config_.log_level_ != LogLevel::kNull) {
        InitLogger(config.config_.log_level_);
    }

    // Use updated IMaster API: pass full QuicServerConfig
    master_->InitAsServer(config, params_, connection_state_cb_);
    return true;
}

void QuicServer::Join() {
    Quic::Join();
}

void QuicServer::Destroy() {
    Quic::Destroy();
}

void QuicServer::AddTimer(uint32_t timeout_ms, std::function<void()> cb) {
    master_->AddTimer(timeout_ms, cb);
}

bool QuicServer::ListenAndAccept(const std::string& ip, uint16_t port) {
    master_->AddListener(ip, port);
    return true;
}

void QuicServer::SetConnectionStateCallBack(connection_state_callback cb) {
    Quic::SetConnectionStateCallBack(cb);
}

}
}