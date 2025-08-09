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

bool QuicServer::Init(const std::string& cert_file, const std::string& key_file, const std::string& alpn,
    uint16_t thread_num, LogLevel level) {
    if (level != LogLevel::kNull) {
        InitLogger(level);
    }
    master_->InitAsServer(thread_num, cert_file, key_file, alpn, params_, connection_state_cb_);
    return true;
}

bool QuicServer::Init(const char* cert_pem, const char* key_pem, const std::string& alpn,
    uint16_t thread_num, LogLevel level) {
    if (level != LogLevel::kNull) {
        InitLogger(level);
    }
    master_->InitAsServer(thread_num, cert_pem, key_pem, alpn, params_, connection_state_cb_);
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