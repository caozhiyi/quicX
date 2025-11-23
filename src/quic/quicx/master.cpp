#include "common/log/log.h"
#include "quic/quicx/master.h"

namespace quicx {
namespace quic {

Master::Master(bool ecn_enabled, std::shared_ptr<common::IEventLoop> event_loop):
    ecn_enabled_(ecn_enabled) {
    receiver_ = IReceiver::MakeReceiver(event_loop);
    if (!receiver_) {
        common::LOG_ERROR("Master::Master: failed to create receiver");
    }
}

void Master::Init() {
    receiver_->SetEcnEnabled(ecn_enabled_);

    common::LOG_DEBUG("Master::Init: processing %zu pending listeners", pending_listeners_.size());
    for (auto& info : pending_listeners_) {
        if (info.sock != -1) {
            common::LOG_DEBUG("Master::Init: adding socket fd=%d to receiver", info.sock);
            if (!receiver_->AddReceiver(info.sock, shared_from_this())) {
                common::LOG_ERROR("Master::Init: failed to add socket fd=%d to receiver", info.sock);
            }
        } else {
            common::LOG_DEBUG("Master::Init: adding listener %s:%d to receiver", info.ip.c_str(), info.port);
            if (!receiver_->AddReceiver(info.ip, info.port, shared_from_this())) {
                common::LOG_ERROR("Master::Init: failed to add listener %s:%d to receiver", info.ip.c_str(), info.port);
            }
        }
    }
    pending_listeners_.clear();
}

Master::~Master() {}

void Master::AddWorker(std::shared_ptr<IWorker> worker) {
    worker->SetConnectionIDNotify(shared_from_this());
    worker_map_.emplace(worker->GetWorkerId(), worker);
}

bool Master::AddListener(int32_t listener_sock) {
    if (!receiver_) {
        common::LOG_DEBUG("Master::AddListener: receiver not initialized, adding socket fd=%d to pending_listeners_", listener_sock);
        ListenerInfo info;
        info.sock = listener_sock;
        pending_listeners_.push_back(info);
        return true;
    }
    common::LOG_DEBUG("Master::AddListener: receiver initialized, adding socket fd=%d directly", listener_sock);
    return receiver_->AddReceiver(listener_sock, shared_from_this());
}

bool Master::AddListener(const std::string& ip, uint16_t port) {
    if (!receiver_) {
        ListenerInfo info;
        info.ip = ip;
        info.port = port;
        pending_listeners_.push_back(info);
        return true;
    }
    return receiver_->AddReceiver(ip, port, shared_from_this());
}

void Master::AddConnectionID(ConnectionID& cid, const std::string& worker_id) {
    cid_worker_map_[cid.Hash()] = worker_id;
}

void Master::RetireConnectionID(ConnectionID& cid, const std::string& worker_id) {
    cid_worker_map_.erase(cid.Hash());
}

void Master::OnPacket(std::shared_ptr<NetPacket>& pkt) {
    if (pkt->GetData()->GetDataLength() == 0) {
        return;
    }

    if (!ecn_enabled_) {
        // If ECN is disabled, zero the ECN field to avoid propagating
        pkt->SetEcn(0);
    }
    PacketParseResult packet_info;
    if (MsgParser::ParsePacket(pkt, packet_info)) {
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

}  // namespace quic
}  // namespace quicx
