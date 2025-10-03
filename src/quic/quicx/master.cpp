#include "quic/quicx/master.h"

namespace quicx {
namespace quic {

Master::Master(std::shared_ptr<common::IEventLoop> event_loop, bool ecn_enabled):
    ecn_enabled_(ecn_enabled),
    event_loop_(event_loop) {
    
    receiver_ = IReceiver::MakeReceiver(event_loop);
    receiver_->SetEcnEnabled(ecn_enabled);
}

Master::~Master() {

}

void Master::AddWorker(std::shared_ptr<IWorker> worker) {
    worker->SetConnectionIDNotify(shared_from_this());
    worker_map_.emplace(worker->GetWorkerId(), worker);
}

void Master::AddListener(int32_t listener_sock) {
    receiver_->AddReceiver(listener_sock, shared_from_this());
}

void Master::AddListener(const std::string& ip, uint16_t port) {
    receiver_->AddReceiver(ip, port, shared_from_this());
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
    PacketInfo packet_info;
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

}
}
