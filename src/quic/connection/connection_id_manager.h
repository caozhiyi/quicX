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