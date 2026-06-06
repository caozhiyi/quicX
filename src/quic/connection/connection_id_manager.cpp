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
    // FIX: Do NOT auto-generate a CID here. The previous behavior of calling Generator()
    // when the pool is empty was a bug for the *remote* manager: a remote-CID pool empty
    // condition is a protocol error, never a license to fabricate a peer CID. The local
    // manager always has at least one entry by the time GetCurrentID is called, since
    // callers explicitly Generator() during dial/accept setup.
    if (sequence_cid_map_.empty()) {
        LOG_ERROR("ConnectionIDManager::GetCurrentID called on empty pool, returning stale cur_id_");
    }
    return cur_id_;
}

void ConnectionIDManager::SetCurrentID(const uint8_t* id, uint16_t len, uint64_t sequence) {
    // Replace cur_id_ in-place WITHOUT mutating sequence_cid_map_ or cur_sequence_number_.
    // This is used on the client during handshake to switch the active DCID from the
    // randomly generated ODCID placeholder to the server-issued SCID (RFC 9000 §7.2).
    // The replacement does NOT live in the map: the map is reserved for CIDs delivered
    // via NEW_CONNECTION_ID frames (sequence >= 1) so that frame-driven AddID/RetireID
    // operations cannot collide with the placeholder.
    cur_id_ = ConnectionID(const_cast<uint8_t*>(id), len, sequence);
}

bool ConnectionIDManager::RetireIDBySequence(uint64_t sequence) {
    // RFC 9000 §19.16: RETIRE_CONNECTION_ID retires *exactly one* CID identified by
    // sequence_number. Earlier this function eagerly removed every entry with
    // sequence_number <= sequence which corrupted the local CID pool when the peer
    // retired CIDs out of order (the typical case during connection migration).
    // For batch retirement triggered by NEW_CONNECTION_ID.retire_prior_to use
    // RetireIDsUpTo() instead.
    auto iter = sequence_cid_map_.find(sequence);
    if (iter == sequence_cid_map_.end()) {
        LOG_DEBUG("ConnectionIDManager::RetireIDBySequence: seq=%llu not in pool, ignoring", sequence);
        return false;
    }

    bool cur_retire = (iter->second == cur_id_);
    if (retire_connection_id_cb_) {
        retire_connection_id_cb_(iter->second);
    }
    sequence_cid_map_.erase(iter);

    LOG_DEBUG("ConnectionIDManager::RetireIDBySequence: retired seq=%llu, map_size=%zu, cur_retire=%d",
        sequence, sequence_cid_map_.size(), cur_retire ? 1 : 0);

    if (cur_retire && !sequence_cid_map_.empty()) {
        cur_id_ = sequence_cid_map_.begin()->second;
    } else if (cur_retire) {
        // Pool is now empty AND we just retired the active CID. The caller is
        // responsible for replenishing the pool (peer must have provided a
        // replacement via NEW_CONNECTION_ID before this point per RFC 9000 §5.1.2).
        LOG_WARN(
            "ConnectionIDManager::RetireIDBySequence: pool exhausted after retiring active seq=%llu",
            sequence);
        return false;
    }
    return true;
}

bool ConnectionIDManager::RetireIDsUpTo(uint64_t prior_to) {
    // Batch retirement: retire every entry with sequence_number < prior_to.
    // This is used when applying the retire_prior_to field carried by NEW_CONNECTION_ID.
    if (prior_to == 0) {
        return false;
    }
    bool cur_retire = false;
    for (auto iter = sequence_cid_map_.begin(); iter != sequence_cid_map_.end();) {
        if (iter->second.GetSequenceNumber() < prior_to) {
            if (iter->second == cur_id_) {
                cur_retire = true;
            }
            if (retire_connection_id_cb_) {
                retire_connection_id_cb_(iter->second);
            }
            iter = sequence_cid_map_.erase(iter);
        } else {
            ++iter;
        }
    }

    if (cur_retire && !sequence_cid_map_.empty()) {
        cur_id_ = sequence_cid_map_.begin()->second;
    } else if (cur_retire) {
        LOG_WARN("ConnectionIDManager::RetireIDsUpTo: pool exhausted after batch retire prior_to=%llu",
            prior_to);
        return false;
    }
    return true;
}

bool ConnectionIDManager::AddID(ConnectionID& id) {
    sequence_cid_map_[id.GetSequenceNumber()] = id;
    if (sequence_cid_map_.size() == 1) {
        cur_id_ = id;
    }
    LOG_DEBUG("ConnectionIDManager::AddID: seq=%llu, hash=%llu, map_size=%zu", id.GetSequenceNumber(),
        id.Hash(), sequence_cid_map_.size());
    if (add_connection_id_cb_) {
        add_connection_id_cb_(id);
    }
    return true;
}

bool ConnectionIDManager::AddID(const uint8_t* id, uint16_t len) {
    // Design note: the auto-incrementing sequence number assigned here is
    // meaningful only for the *local* manager (where this endpoint chooses
    // its own sequence numbers). Calling this overload on the *remote*
    // manager would pollute sequence numbers and could collide with
    // sequences chosen by the peer in NEW_CONNECTION_ID frames — remote
    // manager call sites must use the AddID(ConnectionID&) overload with
    // the peer-supplied sequence number instead. A future refactor that
    // splits LocalCIDManager and RemoteCIDManager into distinct types
    // (tracked in learning_project_roadmap.md §2) would let the type
    // system enforce this distinction; for now it is a documented
    // call-site contract.
    ConnectionID conn_id((uint8_t*)id, len, ++cur_sequence_number_);
    return AddID(conn_id);
}

bool ConnectionIDManager::UseNextID() {
    if (sequence_cid_map_.empty() || sequence_cid_map_.size() == 1) {
        return false;
    }

    // Remember the current CID's sequence and identity so we can identify which entry to retire.
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

    if (!found_next) {
        // No alternative found despite map_size >= 2 (shouldn't happen, but be defensive).
        return false;
    }

    // Switch to next CID first so subsequent packets use the new DCID.
    cur_id_ = next_cid;

    // FIX: Retire ONLY the old CID's entry, not "everything <= old_seq". The previous
    // behavior of RetireIDBySequence(old_seq) accidentally evicted the freshly chosen
    // next_cid as well whenever the map happened to contain entries with sequence == old_seq
    // bound to the new CID (which RotateRemoteConnectionID would then trip over by calling
    // GetCurrentID() on an empty map and previously falling through to Generator()).
    // Erase the specific entry whose value equals old_cid, then notify peer.
    for (auto iter = sequence_cid_map_.begin(); iter != sequence_cid_map_.end(); ++iter) {
        if (iter->second == old_cid) {
            if (retire_connection_id_cb_) {
                retire_connection_id_cb_(iter->second);
            }
            sequence_cid_map_.erase(iter);
            break;
        }
    }
    (void)old_seq;  // sequence is implicit in the entry we just erased

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
