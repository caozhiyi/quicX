#include "quic/connection/connection_id_manager.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {

ConnectionID ConnectionIDManager::Generator() {

}

bool ConnectionIDManager::RetireID(ConnectionID& id) {
    uint64_t id_hash = ConnectionIDGenerator::Instance().Hash(id._id, id._len);
    auto iter = _ids_map.find(id_hash);
    if (iter == _ids_map.end()) {
        return false;
    }
    _ids_map.erase(iter);
    return true;
}

bool ConnectionIDManager::AddID(ConnectionID& id) {
    // auto key = ConnectionIDGenerator::Instance().Hash(id._id, id._len);
    // auto value = std::string((char*)id._id, id._len);
    // _ids_map.insert(key, value);
    return true;
}

}
