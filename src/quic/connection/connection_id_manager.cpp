#include "quic/connection/connection_id_manager.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {

uint64_t ConnectionID::Hash() {
    if (_hash == 0) {
        _hash = ConnectionIDGenerator::Instance().Hash(_id, _len);
    }
    return _hash;
}

ConnectionID ConnectionIDManager::Generator() {
    // TODO
}

bool ConnectionIDManager::RetireID(ConnectionID& id) {
    auto iter = _ids_map.find(id.Hash());
    if (iter == _ids_map.end()) {
        return false;
    }
    _ids_map.erase(iter);
    return true;
}

bool ConnectionIDManager::AddID(ConnectionID& id) {
    _ids_map[id.Hash()] = std::string((char*)id._id, id._len);
    return true;
}

}
