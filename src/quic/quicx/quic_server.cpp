#include "common/log/file_logger.h"
#include "common/log/log.h"

#include "quic/crypto/tls/tls_ctx_server.h"
#include "quic/quicx/quic_server.h"
#include "quic/quicx/worker_server.h"
#include "quic/quicx/worker_with_thread.h"

namespace quicx {

std::shared_ptr<IQuicServer> IQuicServer::Create(const QuicTransportParams& params) {
    return std::make_shared<quic::QuicServer>(params);
}

namespace quic {

QuicServer::QuicServer(const QuicTransportParams& params):
    params_(params) {}

QuicServer::~QuicServer() {}

bool QuicServer::Init(const QuicServerConfig& config) {
    if (config.config_.log_level_ != LogLevel::kNull) {
        // std::shared_ptr<common::Logger> log = std::make_shared<common::StdoutLogger>();
        std::shared_ptr<common::FileLogger> file_log = std::make_shared<common::FileLogger>("server.log");
        // file_log->SetLogger(log);
        common::LOG_SET(file_log);
        common::LOG_SET_LEVEL(common::LogLevel(config.config_.log_level_));
    }

    auto tls_ctx = std::make_shared<TLSServerCtx>();
    if (config.cert_file_ != "" && config.key_file_ != "") {
        if (!tls_ctx->Init(
                config.cert_file_, config.key_file_, config.config_.enable_0rtt_, config.session_ticket_timeout_)) {
            common::LOG_ERROR("tls ctx init faliled.");
            return false;
        }
    } else if (config.cert_pem_ != nullptr && config.key_pem_ != nullptr) {
        if (!tls_ctx->Init(
                config.cert_pem_, config.key_pem_, config.config_.enable_0rtt_, config.session_ticket_timeout_)) {
            common::LOG_ERROR("tls ctx init faliled.");
            return false;
        }

    } else {
        common::LOG_ERROR("cert file or key file is not set.");
        return false;
    }

    master_event_loop_ = common::MakeEventLoop();
    if (!master_event_loop_) {
        common::LOG_ERROR("create event loop failed.");
        return false;
    }

    master_ = std::make_shared<MasterWithThread>(config.config_.enable_ecn_, master_event_loop_);
    master_->Start();

    auto sender = ISender::MakeSender();
    worker_map_.reserve(config.config_.worker_thread_num_);
    if (config.config_.thread_mode_ == ThreadMode::kSingleThread) {
        auto worker =
            std::make_shared<ServerWorker>(config, tls_ctx, sender, params_, connection_state_cb_, master_event_loop_);
        master_event_loop_->RunInLoop(
            [worker, this]() { master_event_loop_->AddFixedProcess(std::bind(&ServerWorker::Process, worker)); });

        worker->SetConnectionIDNotify(master_);
        worker_map_[worker->GetWorkerId()] = worker;
        master_->AddWorker(worker);

    } else {
        for (size_t i = 0; i < config.config_.worker_thread_num_; i++) {
            auto worker_loop = common::MakeEventLoop();
            if (!worker_loop) {
                common::LOG_ERROR("create event loop failed.");
                return false;
            }

            auto worker_ptr =
                std::make_shared<ServerWorker>(config, tls_ctx, sender, params_, connection_state_cb_, worker_loop);
            worker_ptr->SetConnectionIDNotify(master_);

            auto worker = std::make_shared<WorkerWithThread>(worker_loop, worker_ptr);
            worker->Start();

            worker_map_[worker->GetWorkerId()] = worker;
            master_->AddWorker(worker);
        }
    }

    return true;
}

void QuicServer::Join() {
    if (master_) {
        master_->Join();
    }
}

void QuicServer::Destroy() {
    if (master_) {
        master_->Stop();
    }
}

void QuicServer::AddTimer(uint32_t timeout_ms, std::function<void()> cb) {
    master_event_loop_->RunInLoop([this, timeout_ms, cb]() { master_event_loop_->AddTimer(cb, timeout_ms); });
}

bool QuicServer::ListenAndAccept(const std::string& ip, uint16_t port) {
    if (master_) {
        return master_->AddListener(ip, port);
    }
    return false;
}

void QuicServer::SetConnectionStateCallBack(connection_state_callback cb) {
    connection_state_cb_ = cb;
}

}  // namespace quic
}  // namespace quicx