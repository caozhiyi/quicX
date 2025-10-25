#include "quic/connection/connection_id_manager.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace quic {

ConnectionID ConnectionIDManager::Generator() {
    ConnectionID id;
    ConnectionIDGenerator::Instance().Generator(id.id_, id.length_);
    id.sequence_number_ = ++cur_sequence_number_;
    AddID(id);
    return id;
}

ConnectionID& ConnectionIDManager::GetCurrentID() {
    if (sequence_cid_map_.empty()) {
        Generator();
    }
    return cur_id_;
}

bool ConnectionIDManager::RetireIDBySequence(uint64_t sequence) {
    bool cur_retire = false;
    for (auto iter = sequence_cid_map_.begin(); iter != sequence_cid_map_.end();) {
        if (iter->second == cur_id_) {
            cur_retire = true;
        }
        if (iter->second.GetSequenceNumber() <= sequence) {
            if (retire_connection_id_cb_) {
                retire_connection_id_cb_(iter->second);
            }
            iter = sequence_cid_map_.erase(iter);
        } else {
            break;
        }
    }
    
    if (cur_retire && !sequence_cid_map_.empty()){
        cur_id_ = sequence_cid_map_.begin()->second;
    } else {
        return false;
    }
    return true;
}

bool ConnectionIDManager::AddID(ConnectionID& id) {
    sequence_cid_map_[id.GetSequenceNumber()] = id;
    if (sequence_cid_map_.size() == 1) { 
        cur_id_ = id;
    }
    if (add_connection_id_cb_) {
        add_connection_id_cb_(id);
    }
    return true;
}

bool ConnectionIDManager::AddID(const uint8_t* id, uint16_t len) {
    ConnectionID conn_id((uint8_t*)id, len, ++cur_sequence_number_);
    return AddID(conn_id);
}

bool ConnectionIDManager::UseNextID() {
    if (sequence_cid_map_.empty() || sequence_cid_map_.size() == 1) {
        return false;
    }

    RetireIDBySequence(cur_id_.GetSequenceNumber()); // retire current id
    return true;
}

std::vector<uint64_t> ConnectionIDManager::GetAllIDHashes() {
    std::vector<uint64_t> hashes;
    for (auto& pair : sequence_cid_map_) {
        hashes.push_back(pair.second.Hash());
    }
    return hashes;
}

}
}