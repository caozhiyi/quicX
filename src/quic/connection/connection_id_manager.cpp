#include "quic/connection/connection_id_manager.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {

ConnectionID ConnectionIDManager::Generator() {

}

bool ConnectionIDManager::RetireID(ConnectionID& id) {
    uint64_t id_hash = ConnectionIDGenerator::Instance().Hash(id._id, id._len);
    auto iter = _ids.find(id_hash);
    if (iter == _ids.end()) {
        return false;
    }
    _ids.erase(iter);
}

bool ConnectionIDManager::AddID(ConnectionID& id) {
    _ids.insert(ConnectionIDGenerator::Instance().Hash(id._id, id._len), std::move(std::string(id._id, id._len)));
}

}
