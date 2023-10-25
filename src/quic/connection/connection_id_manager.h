#ifndef QUIC_CONNECTION_CONNECTION_ID_MANAGER
#define QUIC_CONNECTION_CONNECTION_ID_MANAGER

#include <string>
#include <cstdint>
#include <unordered_map>
#include "common/util/singleton.h"

namespace quicx {

struct ConnectionID {
    uint8_t* _id;
    uint32_t _len;
    uint64_t _hash;
    ConnectionID() : _id(nullptr), _len(0), _hash(0) {}
    ConnectionID(uint8_t* id, uint32_t len) : _id(id), _len(len), _hash(0) {}
    uint64_t Hash();
};

class ConnectionIDManager {
public:
    ConnectionIDManager() {}
    ~ConnectionIDManager() {}

    ConnectionID Generator();
    bool RetireID(ConnectionID& id);
    bool AddID(ConnectionID& id);

private: 
    std::unordered_map<uint64_t, std::string> _ids_map;
};

}

#endif