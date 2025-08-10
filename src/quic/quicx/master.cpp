#include "common/log/log.h"
#include "quic/quicx/master.h"
#include "quic/udp/if_receiver.h"
#include "common/buffer/buffer.h"
#include "quic/common/constants.h"
#include "quic/quicx/worker_client.h"
#include "quic/crypto/tls/tls_ctx_client.h"
#include "quic/crypto/tls/tls_ctx_server.h"

namespace quicx {
namespace quic {

Master::Master():
    ecn_enabled_(false) {
    timer_ = common::MakeTimer();
    receiver_ = IReceiver::MakeReceiver();
}

Master::~Master() {

}

bool Master::InitAsClient(const QuicConfig& config, const QuicTransportParams& params, connection_state_callback connection_state_cb) {
    auto tls_ctx = std::make_shared<TLSClientCtx>();
    if (!tls_ctx->Init()) {
        common::LOG_ERROR("tls ctx init faliled.");
        return false;
    }

    ecn_enabled_ = config.enable_ecn_;

    worker_map_.reserve(config.thread_num_);
    for (size_t i = 0; i < config.thread_num_; i++) {
        auto worker = IWorker::MakeWorker(IWorker::kClientWorker, ecn_enabled_, tls_ctx, params, connection_state_cb);
        worker->Init(shared_from_this());
        worker_map_.emplace(worker->GetCurrentThreadId(), worker);
    }
    // propagate ECN enable to receiver
    receiver_->SetEcnEnabled(ecn_enabled_);

    Start();
    return true;
}

bool Master::InitAsServer(const QuicConfig& config, const std::string& cert_file, const std::string& key_file, const std::string& alpn, 
    const QuicTransportParams& params, connection_state_callback connection_state_cb) {
    auto tls_ctx = std::make_shared<TLSServerCtx>();
    if (!tls_ctx->Init(cert_file, key_file)) {
        common::LOG_ERROR("tls ctx init faliled.");
        return false;
    }

    ecn_enabled_ = config.enable_ecn_;

    worker_map_.reserve(config.thread_num_);
    for (size_t i = 0; i < config.thread_num_; i++) {
        auto worker = IWorker::MakeWorker(IWorker::kServerWorker, ecn_enabled_, tls_ctx, params, connection_state_cb);
        worker->Init(shared_from_this());
        worker_map_.emplace(worker->GetCurrentThreadId(), worker);
    }
    receiver_->SetEcnEnabled(ecn_enabled_);

    Start();
    return true;
}

bool Master::InitAsServer(const QuicConfig& config, const char* cert_pem, const char* key_pem, const std::string& alpn, 
    const QuicTransportParams& params, connection_state_callback connection_state_cb) {
    auto tls_ctx = std::make_shared<TLSServerCtx>();
    if (!tls_ctx->Init(cert_pem, key_pem)) {
        common::LOG_ERROR("tls ctx init faliled.");
        return false;
    }

    ecn_enabled_ = config.enable_ecn_;

    worker_map_.reserve(config.thread_num_);
    for (size_t i = 0; i < config.thread_num_; i++) {
        auto worker = IWorker::MakeWorker(IWorker::kServerWorker, ecn_enabled_,tls_ctx, params, connection_state_cb);
        worker->Init(shared_from_this());
        worker_map_.emplace(worker->GetCurrentThreadId(), worker);
    }
    receiver_->SetEcnEnabled(ecn_enabled_);

    Start();
    return true;
}

void Master::AddTimer(uint32_t timeout_ms, std::function<void()> cb) {
    common::TimerTask task(cb);
    timer_->AddTimer(task, timeout_ms);
}

void Master::Destroy() {
    Stop();
    Weakup();
}

void Master::Weakup() {
    receiver_->Wakeup();
}

void Master::Join() {
    Thread::Join();
}

bool Master::Connection(const std::string& ip, uint16_t port,
    const std::string& alpn, int32_t timeout_ms) {
    if (!worker_map_.empty()) {
        auto iter = worker_map_.begin();
        std::advance(iter, rand() % worker_map_.size());
        auto worker = std::dynamic_pointer_cast<ClientWorker>(iter->second);
        worker->Push([ip, port, alpn, timeout_ms, worker]() {
            worker->Connect(ip, port, alpn, timeout_ms);
        });
        worker->Weakup();
        return true;
    }
    return false;
}

bool Master::Connection(const std::string& ip, uint16_t port,
    const std::string& alpn, int32_t timeout_ms, const std::string& resumption_session_der) {
    if (!worker_map_.empty()) {
        auto iter = worker_map_.begin();
        std::advance(iter, rand() % worker_map_.size());
        auto worker = std::dynamic_pointer_cast<ClientWorker>(iter->second);
        worker->Push([ip, port, alpn, timeout_ms, worker, resumption_session_der]() {
            worker->Connect(ip, port, alpn, timeout_ms, resumption_session_der);
        });
        worker->Weakup();
        return true;
    }
    return false;
}

void Master::AddListener(uint64_t listener_sock) {
    receiver_->AddReceiver(listener_sock);
}

void Master::AddListener(const std::string& ip, uint64_t port) {
    receiver_->AddReceiver(ip, port);
}

void Master::AddConnectionID(ConnectionID& cid) {
    connection_op_queue_.Push({ADD_CONNECTION_ID, cid, std::this_thread::get_id()});
    Weakup();
}

void Master::RetireConnectionID(ConnectionID& cid) {
    connection_op_queue_.Push({RETIRE_CONNECTION_ID, cid, std::this_thread::get_id()});
    Weakup();
}

void Master::Run() {
    while (!IsStop()) {
        DoRecv();
        DoUpdateConnectionID();
    }
}

void Master::DoRecv() {
    uint8_t recv_buf[kMaxV4PacketSize] = {0};
    std::shared_ptr<NetPacket> packet = std::make_shared<NetPacket>();
    auto buffer = std::make_shared<common::Buffer>(recv_buf, sizeof(recv_buf));
    packet->SetData(buffer);

    receiver_->TryRecv(packet, 10000); // TODO add timeout to config
    
    if (packet->GetData()->GetDataLength() > 0) {
        if (!ecn_enabled_) {
            // If ECN is disabled, zero the ECN field to avoid propagating
            packet->SetEcn(0);
        }
        PacketInfo packet_info;
        if (MsgParser::ParsePacket(packet, packet_info)) {
            if (!ecn_enabled_) {
                packet_info.ecn_ = 0;
            }
            auto iter = cid_worker_map_.find(packet_info.cid_.Hash());
            if (iter != cid_worker_map_.end()) {
                auto worker = worker_map_.find(iter->second);
                if (worker != worker_map_.end()) {
                    worker->second->HandlePacket(packet_info);
                }
                
            } else {
                // random find a worker to handle packet
                auto iter = worker_map_.begin();
                std::advance(iter, rand() % worker_map_.size());
                auto worker = std::dynamic_pointer_cast<ClientWorker>(iter->second);
                worker->HandlePacket(packet_info);
            }
        }
    }
}

void Master::DoUpdateConnectionID() {
    ConnectionOpInfo op_info;
    while (connection_op_queue_.Pop(op_info)) {
        if (op_info.operation_ == ADD_CONNECTION_ID) {
            cid_worker_map_[op_info.cid_.Hash()] = op_info.worker_id_;
        } else if (op_info.operation_ == RETIRE_CONNECTION_ID) {
            cid_worker_map_.erase(op_info.cid_.Hash());
        }
    }
}

}
}
