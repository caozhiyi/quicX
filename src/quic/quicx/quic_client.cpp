#include "common/log/log.h"
#include "quic/udp/if_sender.h"
#include "quic/quicx/quic_client.h"
#include "common/network/io_handle.h"
#include "quic/quicx/worker_client.h"
#include "common/log/stdout_logger.h"
#include "quic/quicx/worker_with_thread.h"
#include "quic/connection/session_cache.h"
#include "quic/crypto/tls/tls_ctx_client.h"

namespace quicx {

std::shared_ptr<IQuicClient> IQuicClient::Create(const QuicTransportParams& params) {
    return std::make_shared<quic::QuicClient>(params);
}

namespace quic {

QuicClient::QuicClient(const QuicTransportParams& params):
    params_(params) {

}

QuicClient::~QuicClient() {

}

bool QuicClient::Init(const QuicClientConfig& config) {
    if (config.config_.log_level_ != LogLevel::kNull) {
        std::shared_ptr<common::Logger> log = std::make_shared<common::StdoutLogger>();
        common::LOG_SET(log);
        common::LOG_SET_LEVEL(common::LogLevel(config.config_.log_level_));
    }

    auto tls_ctx = std::make_shared<TLSClientCtx>();
    if (!tls_ctx->Init(config.config_.enable_0rtt_)) {
        common::LOG_ERROR("tls ctx init faliled.");
        return false;
    }

    if (config.enable_session_cache_) {
        SessionCache::Instance().Init(config.session_cache_path_);
    }

    master_ = std::make_shared<MasterWithThread>(config.config_.enable_ecn_);
    master_->Start();

    // client need a socket to send packet
    auto sock_ret = common::UdpSocket();
    if (sock_ret.errno_ != 0) {
        common::LOG_ERROR("create udp socket failed. err:%d", sock_ret.errno_);
        return false;
    }
    int32_t sockfd = sock_ret.return_value_;

    auto nonblock_ret = common::SocketNoblocking(sockfd);
    if (nonblock_ret.errno_ != 0) {
        common::LOG_ERROR("set non block failed. err:%d", nonblock_ret.errno_);
        return false;
    }

    auto sender = ISender::MakeSender(sockfd);

    worker_map_.reserve(config.config_.worker_thread_num_);
    if (config.config_.thread_mode_ == ThreadMode::kSingleThread) {
        auto worker = std::make_shared<ClientWorker>(
            config.config_, tls_ctx, sender, params_, master_->GetEventLoop(), connection_state_cb_);
        master_->GetEventLoop()->AddFixedProcess(std::bind(&ClientWorker::Process, worker));

        worker->SetConnectionIDNotify(master_);
        worker_map_[worker->GetWorkerId()] = worker;
        master_->AddWorker(worker);

    } else {
        for (size_t i = 0; i < config.config_.worker_thread_num_; i++) {
            auto worker_ptr = std::make_unique<ClientWorker>(
                config.config_, tls_ctx, sender, params_, master_->GetEventLoop(), connection_state_cb_);
            worker_ptr->SetConnectionIDNotify(master_);

            auto worker = std::make_shared<WorkerWithThread>(std::move(worker_ptr));
            worker->Start();

            worker_map_[worker->GetWorkerId()] = worker;
            master_->AddWorker(worker);
        }
    }

    // add socket to receiver
    if (!master_->AddListener(sender->GetSocket())) {
        common::LOG_ERROR("add listener failed. err:%d", sender->GetSocket());
        return false;
    }
    return true;
}

void QuicClient::Join() {
    master_->Join();
}

void QuicClient::Destroy() {
    master_->Stop();
}

void QuicClient::AddTimer(uint32_t timeout_ms, std::function<void()> cb) {
    master_->GetEventLoop()->AddTimer(cb, timeout_ms);
}

bool QuicClient::Connection(const std::string& ip, uint16_t port,
    const std::string& alpn, int32_t timeout_ms) {
    if (!worker_map_.empty()) {
        auto iter = worker_map_.begin();
        std::advance(iter, rand() % worker_map_.size());
        auto worker = std::dynamic_pointer_cast<ClientWorker>(iter->second);
        worker->GetEventLoop()->PostTask([ip, port, alpn, timeout_ms, worker]() {
                worker->Connect(ip, port, alpn, timeout_ms);
            });
        return true;
    }
    return false;
}

bool QuicClient::Connection(const std::string& ip, uint16_t port,
    const std::string& alpn, int32_t timeout_ms, const std::string& resumption_session_der) {
    if (!worker_map_.empty()) {
        auto iter = worker_map_.begin();
        std::advance(iter, rand() % worker_map_.size());
        auto worker = std::dynamic_pointer_cast<ClientWorker>(iter->second);
        worker->GetEventLoop()->PostTask([ip, port, alpn, timeout_ms, worker, resumption_session_der]() {
                worker->Connect(ip, port, alpn, timeout_ms, resumption_session_der);
            });
        return true;
    }
    return false;
}

void QuicClient::SetConnectionStateCallBack(connection_state_callback cb) {
    connection_state_cb_ = cb;
}

}
}