#include "quic/quicx/master.h"
#include "common/util/random.h"
#include "common/log/log.h"

namespace quicx {
namespace quic {

Master::Master(bool ecn_enabled, std::shared_ptr<common::IEventLoop> event_loop):
    ecn_enabled_(ecn_enabled) {
    receiver_ = IReceiver::MakeReceiver(event_loop);
    if (!receiver_) {
        LOG_ERROR("Master::Master: failed to create receiver");
    }
}

void Master::Init() {
    receiver_->SetEcnEnabled(ecn_enabled_);

    LOG_DEBUG("Master::Init: processing %zu pending listeners", pending_listeners_.size());
    for (auto& info : pending_listeners_) {
        if (info.sock != -1) {
            LOG_DEBUG("Master::Init: adding socket fd=%d to receiver", info.sock);
            if (!receiver_->AddReceiver(info.sock, shared_from_this())) {
                LOG_ERROR("Master::Init: failed to add socket fd=%d to receiver", info.sock);
            }
        } else {
            LOG_DEBUG("Master::Init: adding listener %s:%d to receiver", info.ip.c_str(), info.port);
            if (!receiver_->AddReceiver(info.ip, info.port, shared_from_this())) {
                LOG_ERROR("Master::Init: failed to add listener %s:%d to receiver", info.ip.c_str(), info.port);
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
        LOG_DEBUG(
            "Master::AddListener: receiver not initialized, adding socket fd=%d to pending_listeners_", listener_sock);
        ListenerInfo info;
        info.sock = listener_sock;
        pending_listeners_.push_back(info);
        return true;
    }
    LOG_DEBUG("Master::AddListener: receiver initialized, adding socket fd=%d directly", listener_sock);
    return receiver_->AddReceiver(listener_sock, shared_from_this());
}

bool Master::RemoveListener(int32_t listener_sock) {
    // Retire a socket (e.g. the pre-migration one) from the poll set.
    if (!receiver_) {
        // Not armed yet: strip it from the pending list if present.
        for (auto it = pending_listeners_.begin(); it != pending_listeners_.end(); ++it) {
            if (it->sock == listener_sock) {
                pending_listeners_.erase(it);
                return true;
            }
        }
        return false;
    }
    LOG_DEBUG("Master::RemoveListener: removing socket fd=%d", listener_sock);
    return receiver_->RemoveReceiver(listener_sock);
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
    std::lock_guard<std::mutex> lock(cid_map_mutex_);
    cid_worker_map_[cid.Hash()] = worker_id;
}

void Master::RetireConnectionID(ConnectionID& cid, const std::string& /*worker_id*/) {
    std::lock_guard<std::mutex> lock(cid_map_mutex_);
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
        // worker_map_ is populated once at init and never mutated at runtime, so
        // reading it without a lock is safe. cid_worker_map_ is written by worker
        // threads during handshake, so every access to it must be locked.
        std::shared_ptr<IWorker> worker;
        {
            std::lock_guard<std::mutex> lock(cid_map_mutex_);
            auto iter = cid_worker_map_.find(packet_info.cid_.Hash());
            if (iter != cid_worker_map_.end()) {
                auto w = worker_map_.find(iter->second);
                if (w != worker_map_.end()) {
                    worker = w->second;
                }
            }
        }

        if (!worker) {
            // New connection: route deterministically by the packet's Destination
            // Connection ID so that *every* Initial for the same connection (including
            // client retransmissions, which reuse the same DCID) lands on the SAME
            // worker. A random choice here let Initial retransmissions create
            // duplicate ServerConnections on different workers under a multi-worker
            // config, which corrupted the handshake. Distinct connections still hash
            // to different workers, so load stays balanced.
            if (worker_map_.empty()) {
                return;
            }
            size_t idx = packet_info.cid_.Hash() % worker_map_.size();
            auto iter = worker_map_.begin();
            std::advance(iter, idx);
            worker = iter->second;
        }

        worker->HandlePacket(packet_info);
    }
}

}  // namespace quic
}  // namespace quicx
