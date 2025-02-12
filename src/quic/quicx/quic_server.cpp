#include "common/log/log.h"
#include "quic/quicx/quic_server.h"
#include "quic/quicx/processor_server.h"
namespace quicx {
namespace quic {

std::shared_ptr<IQuicServer> IQuicServer::Create(const QuicTransportParams& params) {
    return std::make_shared<QuicServer>(params);
}

QuicServer::QuicServer(const QuicTransportParams& params):
    QuicBase(params) {

}

QuicServer::~QuicServer() {

}

bool QuicServer::Init(const std::string& cert_file, const std::string& key_file, const std::string& alpn, uint16_t thread_num, LogLevel level) {
    if (level != LogLevel::kNull) {
        InitLogger(level);
    }
    auto tls_ctx = std::make_shared<TLSServerCtx>();
    if (!tls_ctx->Init(cert_file, key_file)) {
        common::LOG_ERROR("tls ctx init faliled.");
        return false;
    }
    tls_ctx_ = tls_ctx;

    processors_map_.reserve(thread_num);
    for (size_t i = 0; i < thread_num; i++) {
        auto processor = std::make_shared<ProcessorServer>(tls_ctx_, params_, connection_state_cb_);
        processor->SetServerAlpn(alpn);
        processor->Start();
        processors_map_.emplace(processor->GetCurrentThreadId(), processor);
    }
    return true;
}

bool QuicServer::Init(const char* cert_pem, const char* key_pem, const std::string& alpn, uint16_t thread_num, LogLevel level) {
    if (level != LogLevel::kNull) {
        InitLogger(level);
    }
    auto tls_ctx = std::make_shared<TLSServerCtx>();
    if (!tls_ctx->Init(cert_pem, key_pem)) {
        common::LOG_ERROR("tls ctx init faliled.");
        return false;
    }
    tls_ctx_ = tls_ctx;
    
    processors_map_.reserve(thread_num);
    for (size_t i = 0; i < thread_num; i++) {
        auto processor = std::make_shared<ProcessorServer>(tls_ctx_, params_, connection_state_cb_);
        processor->SetServerAlpn(alpn);
        processor->Start();
        processors_map_.emplace(processor->GetCurrentThreadId(), processor);
    }
    return true;
}

void QuicServer::Join() {
    QuicBase::Join();
}

void QuicServer::Destroy() {
    QuicBase::Destroy();
}

void QuicServer::AddTimer(uint32_t interval_ms, timer_callback cb) {
    QuicBase::AddTimer(interval_ms, cb);
}

bool QuicServer::ListenAndAccept(const std::string& ip, uint16_t port) {
    for (auto& processor : processors_map_) {
        processor.second->AddReceiver(ip, port);
    }
    return true;
}

void QuicServer::SetConnectionStateCallBack(connection_state_callback cb) {
    QuicBase::SetConnectionStateCallBack(cb);
}

}
}