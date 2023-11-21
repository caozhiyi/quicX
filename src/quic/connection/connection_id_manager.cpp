#include "quic/connection/connection_id_manager.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace quic {

uint64_t ConnectionID::Hash() {
    if (_hash == 0) {
        _hash = ConnectionIDGenerator::Instance().Hash(_id, _len);
    }
    return _hash;
}

ConnectionID ConnectionIDManager::Generator() {
    ConnectionID id;
    ConnectionIDGenerator::Instance().Generator(id._id, id._len);
    id._index = ++_cur_index;
    AddID(id);
    return id;
}

ConnectionID& ConnectionIDManager::GetCurrentID() {
    return _cur_id;
}

bool ConnectionIDManager::RetireIDBySequence(uint64_t sequence) {
    bool cur_retire = false;
    for (auto iter = _ids_map.begin(); iter != _ids_map.end();) {
        if (iter->second.Hash() == _cur_id.Hash()) {
            cur_retire = true;
        }
        if (iter->second._index <= sequence) {
            if (_retire_connection_id_cb) {
                _retire_connection_id_cb(iter->second.Hash());
            }
            iter = _ids_map.erase(iter);
        }
    }
    
    if (cur_retire && !_ids_map.empty()){
        _cur_id = _ids_map.begin()->second;
    } else {
        return false;
    }
    return true;
}

bool ConnectionIDManager::AddID(ConnectionID& id) {
    _ids_map[id._index] = id;
    if (_ids_map.size() == 1) { 
        _cur_id = id;
    }
    if (_add_connection_id_cb) {
        _add_connection_id_cb(id.Hash());
    }
    return true;
}

}
}