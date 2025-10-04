#include "common/log/log.h"
#include "quic/quicx/quic_server.h"
#include "quic/quicx/worker_server.h"
#include "common/log/stdout_logger.h"
#include "quic/quicx/worker_with_thread.h"
#include "quic/crypto/tls/tls_ctx_server.h"

namespace quicx {
namespace quic {

std::shared_ptr<IQuicServer> IQuicServer::Create(const QuicTransportParams& params) {
    return std::make_shared<QuicServer>(params);
}

QuicServer::QuicServer(const QuicTransportParams& params):
    params_(params) {

}

QuicServer::~QuicServer() {

}

bool QuicServer::Init(const QuicServerConfig& config) {
    if (config.config_.log_level_ != LogLevel::kNull) {
        std::shared_ptr<common::Logger> log = std::make_shared<common::StdoutLogger>();
        common::LOG_SET(log);
        common::LOG_SET_LEVEL(common::LogLevel(config.config_.log_level_));
    }

    auto tls_ctx = std::make_shared<TLSServerCtx>();
    if (config.cert_file_ != "" && config.key_file_ != "") {
        if (!tls_ctx->Init(config.cert_file_, config.key_file_, config.config_.enable_0rtt_, config.session_ticket_timeout_)) {
            common::LOG_ERROR("tls ctx init faliled.");
            return false;
        }
    } else if (config.cert_pem_ != nullptr && config.key_pem_ != nullptr) {
        if (!tls_ctx->Init(config.cert_pem_, config.key_pem_, config.config_.enable_0rtt_, config.session_ticket_timeout_)) {
            common::LOG_ERROR("tls ctx init faliled.");
            return false;
        }

    } else {
        common::LOG_ERROR("cert file or key file is not set.");
        return false;
    }

    master_ = std::make_shared<MasterWithThread>(config.config_.enable_ecn_);
    master_->Start();

    auto sender = ISender::MakeSender();
    worker_map_.reserve(config.config_.worker_thread_num_);
    if (config.config_.thread_mode_ == ThreadMode::kSingleThread) {
        auto worker = std::make_shared<ServerWorker>(
            config, tls_ctx, sender, params_, master_->GetEventLoop(), connection_state_cb_);
        master_->GetEventLoop()->AddFixedProcess(std::bind(&ServerWorker::Process, worker));

        worker->SetConnectionIDNotify(master_);
        worker_map_[worker->GetWorkerId()] = worker;

    } else {
        for (size_t i = 0; i < config.config_.worker_thread_num_; i++) {
            auto worker_ptr = std::make_unique<ServerWorker>(
                config, tls_ctx, sender, params_, master_->GetEventLoop(), connection_state_cb_);
            worker_ptr->SetConnectionIDNotify(master_);

            
            auto worker = std::make_shared<WorkerWithThread>(std::move(worker_ptr));
            worker->Start();

            worker_map_[worker->GetWorkerId()] = worker;
        }
    }

    return true;
}

void QuicServer::Join() {
    master_->Join();
}

void QuicServer::Destroy() {
    master_->Stop();
}

void QuicServer::AddTimer(uint32_t timeout_ms, std::function<void()> cb) {
    master_->GetEventLoop()->AddTimer(cb, timeout_ms);
}

bool QuicServer::ListenAndAccept(const std::string& ip, uint16_t port) {
    return master_->AddListener(ip, port);
}

void QuicServer::SetConnectionStateCallBack(connection_state_callback cb) {
    connection_state_cb_ = cb;
}

}
}