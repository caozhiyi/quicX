#ifndef QUIC_CONNECTION_CONNECTION_ID_MANAGER
#define QUIC_CONNECTION_CONNECTION_ID_MANAGER

#include <map>
#include <string>
#include <cstring>
#include <cstdint>
#include "common/util/singleton.h"

namespace quicx {

static const uint8_t __connection_id_len = 16;

struct ConnectionID {
    uint8_t* _id;
    uint32_t _len;
    uint64_t _hash;
    uint64_t _index;
    ConnectionID(uint64_t index = 0) : _id(nullptr), _len(__connection_id_len), _hash(0), _index(index) {
        _id = new uint8_t[__connection_id_len]; 
        memset(_id, 0, __connection_id_len);
    }
    ConnectionID(uint8_t* id, uint32_t len, uint64_t index) : _id(id), _len(len), _hash(0), _index(index) {}
    ~ConnectionID() {
        if (_id) {
            delete _id;
        }
    }
    uint64_t Hash();
};

class ConnectionIDManager {
public:
    ConnectionIDManager(): _cur_index(0) {}
    ~ConnectionIDManager() {}

    ConnectionID Generator();
    ConnectionID& GetCurrentID();
    bool RetireIDBySequence(uint64_t sequence);
    bool AddID(ConnectionID& id);

private:
    int64_t _cur_index;
    ConnectionID _cur_id;
    std::map<uint64_t, ConnectionID> _ids_map;
};

}

#endif