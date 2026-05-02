#include "quic/connection/connection_id_manager.h"
#include "common/log/log.h"
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

    if (cur_retire && !sequence_cid_map_.empty()) {
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
    common::LOG_DEBUG("ConnectionIDManager::AddID: seq=%llu, hash=%llu, map_size=%zu", id.GetSequenceNumber(),
        id.Hash(), sequence_cid_map_.size());
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

    // Remember the current CID's sequence and identity so we can identify which entries to retire.
    uint64_t old_seq = cur_id_.GetSequenceNumber();
    ConnectionID old_cid = cur_id_;

    // Pick the next CID from the pool that is not equal to the current one.
    // Note: Because remote CIDs are assigned by the peer while the local ConnectionIDManager
    // also auto-increments cur_sequence_number_ in AddID(ptr,len), two different CIDs may
    // share the same key in sequence_cid_map_. We pick the first entry that is NOT the same
    // binary identity as cur_id_.
    ConnectionID next_cid;
    bool found_next = false;
    for (auto& kv : sequence_cid_map_) {
        if (!(kv.second == old_cid)) {
            next_cid = kv.second;
            found_next = true;
            break;
        }
    }

    // Switch to next CID first so subsequent packets use the new DCID.
    if (found_next) {
        cur_id_ = next_cid;
    }

    // Retire old CID via sequence-based retirement (this also notifies peer via RETIRE_CONNECTION_ID cb).
    RetireIDBySequence(old_seq);

    // If RetireIDBySequence happened to reset cur_id_ to map.begin() (because it found an exact
    // binary match for old_cid), but we have a better candidate, restore our chosen next.
    if (found_next && !(cur_id_ == next_cid)) {
        // Only override if next_cid still exists in the map
        for (auto& kv : sequence_cid_map_) {
            if (kv.second == next_cid) {
                cur_id_ = next_cid;
                break;
            }
        }
    }

    return true;
}

std::vector<uint64_t> ConnectionIDManager::GetAllIDHashes() {
    std::vector<uint64_t> hashes;
    for (auto& pair : sequence_cid_map_) {
        hashes.push_back(pair.second.Hash());
    }
    return hashes;
}

}  // namespace quic
}  // namespace quicx