#include "common/log/log.h"
#include "quic/quicx/master.h"
#include "quic/quicx/global_resource.h"

namespace quicx {
namespace quic {

Master::Master(bool ecn_enabled):
    ecn_enabled_(ecn_enabled) {}

void Master::Init() {
    receiver_ = IReceiver::MakeReceiver();
    receiver_->SetEcnEnabled(ecn_enabled_);

    for (auto& info : pending_listeners_) {
        if (info.sock != -1) {
            receiver_->AddReceiver(info.sock, shared_from_this());
        } else {
            receiver_->AddReceiver(info.ip, info.port, shared_from_this());
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
        ListenerInfo info;
        info.sock = listener_sock;
        pending_listeners_.push_back(info);
        return true;
    }
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
