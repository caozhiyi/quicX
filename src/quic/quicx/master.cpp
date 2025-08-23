#include "common/log/log.h"
#include "quic/quicx/master.h"
#include "quic/udp/if_receiver.h"
#include "common/network/io_handle.h"
#include "quic/quicx/worker_client.h"
#include "quic/crypto/tls/tls_ctx_client.h"
#include "quic/crypto/tls/tls_ctx_server.h"

namespace quicx {
namespace quic {

Master::Master():
    ecn_enabled_(false) {
    timer_ = common::MakeTimer();
    receiver_ = IReceiver::MakeReceiver();
    packet_allotor_ = IPacketAllotor::MakePacketAllotor(IPacketAllotor::PacketAllotorType::POOL);
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

    worker_map_.reserve(config.thread_num_);
    for (size_t i = 0; i < config.thread_num_; i++) {
        auto worker = IWorker::MakeClientWorker(config, tls_ctx, sender, params, connection_state_cb);
        worker->Init(shared_from_this());
        worker_map_.emplace(worker->GetCurrentThreadId(), worker);
    }
    
    // add socket to receiver
    receiver_->AddReceiver(sender->GetSocket());

    // propagate ECN enable to receiver
    receiver_->SetEcnEnabled(ecn_enabled_);

    Start();
    return true;
}

bool Master::InitAsServer(const QuicServerConfig& config, const QuicTransportParams& params, connection_state_callback connection_state_cb) {
    auto tls_ctx = std::make_shared<TLSServerCtx>();
    if (config.cert_file_ != "" && config.key_file_ != "") {
        if (!tls_ctx->Init(config.cert_file_, config.key_file_)) {
            common::LOG_ERROR("tls ctx init faliled.");
            return false;
        }
    } else if (config.cert_pem_ != nullptr && config.key_pem_ != nullptr) {
        if (!tls_ctx->Init(config.cert_pem_, config.key_pem_)) {
            common::LOG_ERROR("tls ctx init faliled.");
            return false;
        }

    } else {
        common::LOG_ERROR("cert file or key file is not set.");
        return false;
    }

    ecn_enabled_ = config.config_.enable_ecn_;

    // server don't need a client socket to send packet
    auto sender = ISender::MakeSender();

    worker_map_.reserve(config.config_.thread_num_);
    for (size_t i = 0; i < config.config_.thread_num_; i++) {
        auto worker = IWorker::MakeServerWorker(config, tls_ctx, sender, params, connection_state_cb);
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

void Master::AddListener(int32_t listener_sock) {
    receiver_->AddReceiver(listener_sock);
}

void Master::AddListener(const std::string& ip, uint16_t port) {
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
    std::shared_ptr<NetPacket> packet = packet_allotor_->Malloc();
    receiver_->TryRecv(packet, 10000); // TODO add timeout to config
    
    if (packet->GetData()->GetDataLength() > 0) {
        if (!ecn_enabled_) {
            // If ECN is disabled, zero the ECN field to avoid propagating
            packet->SetEcn(0);
        }
        PacketInfo packet_info;
        if (MsgParser::ParsePacket(packet, packet_info)) {
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
                auto worker = iter->second;
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
