#include "common/log/log.h"
#include "quic/quicx/quic_server.h"

namespace quicx {
namespace quic {


QuicServer::QuicServer() {

}

QuicServer::~QuicServer() {

}

bool QuicServer::Init(const std::string& cert_file, const std::string& key_file, uint16_t thread_num) {
    auto tls_ctx = std::make_shared<TLSServerCtx>();
    if (!tls_ctx->Init(cert_file, key_file)) {
        common::LOG_ERROR("tls ctx init faliled.");
        return false;
    }
    tls_ctx_ = tls_ctx;
    return QuicBase::Init(thread_num);
}

bool QuicServer::Init(const char* cert_pem, const char* key_pem, uint16_t thread_num) {
    auto tls_ctx = std::make_shared<TLSServerCtx>();
    if (!tls_ctx->Init(cert_pem, key_pem)) {
        common::LOG_ERROR("tls ctx init faliled.");
        return false;
    }
    tls_ctx_ = tls_ctx;
    return QuicBase::Init(thread_num);
}

void QuicServer::Join() {
    QuicBase::Join();
}

void QuicServer::Destroy() {
    QuicBase::Destroy();
}

bool QuicServer::ListenAndAccept(const std::string& ip, uint16_t port) {
    for (auto& processor : processors_) {
        processor->AddReceiver(ip, port);
    }
    return true;
}

void QuicServer::SetConnectionStateCallBack(connection_state_callback cb) {
    QuicBase::SetConnectionStateCallBack(cb);
}

}
}