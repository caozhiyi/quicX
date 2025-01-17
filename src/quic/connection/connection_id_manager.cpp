#include "quic/connection/connection_id_manager.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace quic {

uint64_t ConnectionID::Hash() {
    if (hash_ == 0) {
        hash_ = ConnectionIDGenerator::Instance().Hash(id_, len_);
    }
    return hash_;
}

ConnectionID ConnectionIDManager::Generator() {
    ConnectionID id;
    ConnectionIDGenerator::Instance().Generator(id.id_, id.len_);
    id.index_ = ++cur_index_;
    AddID(id);
    return id;
}

ConnectionID& ConnectionIDManager::GetCurrentID() {
    if (ids_map_.empty()) {
        Generator();
    }
    return cur_id_;
}

bool ConnectionIDManager::RetireIDBySequence(uint64_t sequence) {
    bool cur_retire = false;
    for (auto iter = ids_map_.begin(); iter != ids_map_.end();) {
        if (iter->second.Hash() == cur_id_.Hash()) {
            cur_retire = true;
        }
        if (iter->second.index_ <= sequence) {
            if (retire_connection_id_cb_) {
                retire_connection_id_cb_(iter->second.Hash());
            }
            iter = ids_map_.erase(iter);
        } else {
            break;
        }
    }
    
    if (cur_retire && !ids_map_.empty()){
        cur_id_ = ids_map_.begin()->second;
    } else {
        return false;
    }
    return true;
}

bool ConnectionIDManager::AddID(ConnectionID& id) {
    ids_map_[id.index_] = id;
    if (ids_map_.size() == 1) { 
        cur_id_ = id;
    }
    if (add_connection_id_cb_) {
        add_connection_id_cb_(id.Hash());
    }
    return true;
}

bool ConnectionIDManager::AddID(const uint8_t* id, uint16_t len) {
    ConnectionID conn_id((uint8_t*)id, len, ++cur_index_);
    return AddID(conn_id);
}

bool ConnectionIDManager::UseNextID() {
    if (ids_map_.empty() || ids_map_.size() == 1) {
        return false;
    }

    RetireIDBySequence(cur_id_.index_); // retire current id
    return true;
}

}
}