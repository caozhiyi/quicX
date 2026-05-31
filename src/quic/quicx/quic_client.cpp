#include "common/log/file_logger.h"
#include "common/log/log.h"
#include "common/network/io_handle.h"
#include "common/qlog/qlog_manager.h"

#include "quic/connection/connection_base.h"
#include "quic/connection/session_cache.h"
#include "quic/crypto/tls/tls_ctx_client.h"
#include "quic/quicx/quic_client.h"
#include "quic/quicx/worker_client.h"
#include "quic/quicx/worker_with_thread.h"
#include "quic/udp/if_sender.h"

namespace quicx {

std::shared_ptr<IQuicClient> IQuicClient::Create(const QuicTransportParams& params) {
    return std::make_shared<quic::QuicClient>(params);
}

namespace quic {

QuicClient::QuicClient(const QuicTransportParams& params):
    params_(params) {}

QuicClient::~QuicClient() {
    // With the Exclusive Ownership + Observer Reference refactoring:
    //   - Worker/Connection/Stream event_loop_ are now weak_ptr (no upward cycle)
    //   - Timer callbacks use weak_from_this() (no timer→Connection cycle)
    //   - AddFixedProcess uses weak_ptr owner (auto-expires when worker dies)
    // ClearFixedProcesses/ClearAllTimers are no longer needed for cycle-breaking.

    // 0) Stop the master event loop thread so no callbacks can race with
    //    member destruction. After Join, no loop-thread code is running.
    if (master_) {
        master_->Stop();
        master_->Join();
    }

    // 1) In multi-thread mode each worker owns its own event-loop thread
    //    (WorkerWithThread inherits common::Thread). We MUST stop and join
    //    those threads BEFORE worker_map_.clear() destroys the
    //    WorkerWithThread objects, otherwise the worker thread is still
    //    running Run() -> worker_ptr_->Process() while our destructor frees
    //    worker_ptr_ / event_loop_ / packet_queue_ — confirmed by ASan
    //    (heap-use-after-free), TSan (data races) and UBSan (vptr).
    //
    //    NOTE: this MUST happen before step 2 (Shutdown). Shutdown clears
    //    per-worker bookkeeping (handshake_timers_, conn_map_, ...) which
    //    belongs to the worker event loop. If Shutdown ran first the
    //    worker thread would still be executing inside Wait()/Process()
    //    and any AssertInLoopThread()-protected mutation
    //    (EventLoop::RemoveTimer) would abort because Shutdown runs on
    //    the QuicClient-owner thread, not on the worker loop thread.
    //
    //    Note: relying on Thread::~Thread() to do this is unsafe — by the
    //    time the base destructor runs the derived members are already
    //    gone, and Thread::Stop() alone does not wake up the worker's epoll
    //    loop, so the join would block until the next packet.
    for (auto& kv : worker_map_) {
        if (auto wwt = std::dynamic_pointer_cast<WorkerWithThread>(kv.second)) {
            wwt->Stop();   // sets stop_ AND wakes up the worker event loop
            wwt->Join();   // wait for Run() to actually exit
        }
    }

    // 2) Drain per-connection bookkeeping held by each worker (conn_map_,
    //    active_send_connections_, handshake timers). Safe to run on the
    //    owner thread now that every worker's event-loop thread has been
    //    Stop()+Join()'d above — no concurrent access, no timer will fire.
    //    Letting it run before stopping the threads would have invoked
    //    EventLoop::RemoveTimer from the wrong thread and triggered
    //    AssertInLoopThread()'s abort().
    for (auto& kv : worker_map_) {
        if (kv.second) {
            kv.second->Shutdown();
        }
    }

    // 3) Drop strong refs. C++ reverse member destruction handles the rest.
    worker_map_.clear();
    master_.reset();
    master_event_loop_.reset();
}

bool QuicClient::Init(const QuicClientConfig& config) {
    // Always sync the configured level to the logger so that LOG_xxx macros
    // can short-circuit argument evaluation when level is kNull.
    LOG_SET_LEVEL(common::LogLevel(config.config_.log_level_));
    if (config.config_.log_level_ != LogLevel::kNull) {
        // std::shared_ptr<common::Logger> log = std::make_shared<common::StdoutLogger>();
        std::shared_ptr<common::FileLogger> file_log =
            std::make_shared<common::FileLogger>(config.config_.log_path_ + "/client.log");
        // file_log->SetLogger(log);
        LOG_SET(file_log);
    }

    // Initialize QLog if enabled
    if (config.config_.qlog_config_.enabled) {
        common::QlogManager::Instance().SetConfig(config.config_.qlog_config_);
        common::QlogManager::Instance().Enable(true);
        LOG_INFO("QLog enabled. Output dir: %s", config.config_.qlog_config_.output_dir.c_str());
    } else {
        common::QlogManager::Instance().Enable(false);
    }

    auto tls_ctx = std::make_shared<TLSClientCtx>();
    if (!tls_ctx->Init(config.config_.enable_0rtt_, config.config_.cipher_suites_,
                       config.verify_peer_, config.ca_file_)) {
        LOG_ERROR("tls ctx init failed.");
        return false;
    }

    // Enable TLS key logging if configured
    if (!config.config_.keylog_file_.empty()) {
        tls_ctx->EnableKeyLog(config.config_.keylog_file_);
    }

    if (config.enable_session_cache_) {
        SessionCache::Instance().Init(config.session_cache_path_);
    }

    master_event_loop_ = common::MakeEventLoop();
    if (!master_event_loop_) {
        LOG_ERROR("create event loop failed.");
        return false;
    }

    master_ = std::make_shared<MasterWithThread>(config.config_.enable_ecn_, master_event_loop_);
    master_->Start();

    if (!master_->WaitUntilReady()) {
        LOG_ERROR("master event loop init failed.");
        return false;
    }

    // client need a socket to send packet
    auto sock_ret = common::UdpSocket();
    if (sock_ret.error_code_ != 0) {
        LOG_ERROR("create udp socket failed. err:%d", sock_ret.error_code_);
        return false;
    }
    int32_t sockfd = sock_ret.return_value_;

    auto nonblock_ret = common::SocketNoblocking(sockfd);
    if (nonblock_ret.error_code_ != 0) {
        LOG_ERROR("set non block failed. err:%d", nonblock_ret.error_code_);
        return false;
    }

    thread_mode_ = config.config_.thread_mode_;
    auto sender = ISender::MakeSender(sockfd);

    worker_map_.reserve(config.config_.worker_thread_num_);
    if (thread_mode_ == ThreadMode::kSingleThread) {
        auto worker = std::make_shared<ClientWorker>(
            config.config_, tls_ctx, sender, params_, connection_state_cb_, master_event_loop_);
        // Set register socket callback for connection migration
        auto master_weak = std::weak_ptr<MasterWithThread>(master_);
        worker->SetRegisterSocketCallback([master_weak](int32_t sockfd) -> bool {
            auto master = master_weak.lock();
            if (master) {
                return master->AddListener(sockfd);
            }
            return false;
        });
        master_event_loop_->RunInLoop(
            [worker, this]() { master_event_loop_->AddFixedProcess(worker, std::bind(&ClientWorker::Process, worker)); });

        worker->SetConnectionIDNotify(master_);
        worker_map_[worker->GetWorkerId()] = worker;
        master_->AddWorker(worker);

    } else {
        for (size_t i = 0; i < config.config_.worker_thread_num_; i++) {
            auto worker_loop = common::MakeEventLoop();
            if (!worker_loop) {
                LOG_ERROR("create event loop failed.");
                return false;
            }

            auto worker_ptr = std::make_shared<ClientWorker>(
                config.config_, tls_ctx, sender, params_, connection_state_cb_, worker_loop);
            // Set register socket callback for connection migration
            auto master_weak2 = std::weak_ptr<MasterWithThread>(master_);
            worker_ptr->SetRegisterSocketCallback([master_weak2](int32_t sockfd) -> bool {
                auto master = master_weak2.lock();
                if (master) {
                    return master->AddListener(sockfd);
                }
                return false;
            });
            worker_ptr->SetConnectionIDNotify(master_);

            auto worker = std::make_shared<WorkerWithThread>(worker_loop, worker_ptr);
            worker->Start();

            if (!worker->WaitUntilReady()) {
                LOG_ERROR("worker event loop init failed.");
                return false;
            }

            worker_map_[worker->GetWorkerId()] = worker;
            master_->AddWorker(worker);
        }
    }

    // add socket to receiver
    if (!master_->AddListener(sender->GetSocket())) {
        LOG_ERROR("add listener failed. err:%d", sender->GetSocket());
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
    master_event_loop_->RunInLoop([this, timeout_ms, cb]() { master_event_loop_->AddTimer(cb, timeout_ms); });
}

bool QuicClient::Connection(const std::string& ip, uint16_t port, const std::string& alpn, int32_t timeout_ms,
    const std::string& resumption_session_der, const std::string& server_name) {
    if (thread_mode_ == ThreadMode::kSingleThread) {
        if (!worker_map_.empty()) {
            auto iter = worker_map_.begin();
            std::advance(iter, rand() % worker_map_.size());
            auto worker = std::dynamic_pointer_cast<ClientWorker>(iter->second);
            if (master_event_loop_) {
                master_event_loop_->RunInLoop(
                    [ip, port, alpn, timeout_ms, worker, resumption_session_der, server_name]() {
                        worker->Connect(ip, port, alpn, timeout_ms, resumption_session_der, server_name);
                    });
                return true;
            }
        }
        return false;
    }

    if (!worker_map_.empty()) {
        auto iter = worker_map_.begin();
        std::advance(iter, rand() % worker_map_.size());
        auto worker = std::dynamic_pointer_cast<WorkerWithThread>(iter->second);
        if (!worker) {
            return false;
        }
        auto client_worker = std::dynamic_pointer_cast<ClientWorker>(worker->GetWorker());
        if (!client_worker) {
            return false;
        }
        auto event_loop = worker->GetEventLoop();
        if (event_loop) {
            event_loop->RunInLoop([ip, port, alpn, timeout_ms, client_worker, resumption_session_der, server_name]() {
                client_worker->Connect(ip, port, alpn, timeout_ms, resumption_session_der, server_name);
            });
            return true;
        }
    }
    return false;
}

void QuicClient::SetConnectionStateCallBack(connection_state_callback cb) {
    connection_state_cb_ = cb;
}

}  // namespace quic
}  // namespace quicx