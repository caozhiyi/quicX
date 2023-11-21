#ifndef QUIC_CONNECTION_CONNECTION_ID_MANAGER
#define QUIC_CONNECTION_CONNECTION_ID_MANAGER

#include <map>
#include <string>
#include <cstring>
#include <cstdint>
#include "quic/connection/type.h"
#include "common/util/singleton.h"
#include "quic/connection/connection_interface.h"

namespace quicx {
namespace quic {

struct ConnectionID {
    uint8_t _id[__max_cid_length];
    uint8_t _len;
    uint64_t _index;
    uint64_t _hash;
    ConnectionID(uint64_t index = 0): _len(0), _index(index), _hash(0) {
        memset(_id, 0, __max_cid_length);
    }
    ConnectionID(uint8_t* id, uint8_t len, uint64_t index = 0): _len(len), _index(index), _hash(0) {
        memset(_id, 0, __max_cid_length);
        memcpy(_id, id, len);
    }
    ~ConnectionID() {}
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

    virtual void SetAddConnectionIDCB(ConnectionIDCB cb) { _add_connection_id_cb = cb; }
    virtual void SetRetireConnectionIDCB(ConnectionIDCB cb) { _retire_connection_id_cb = cb; }

private:
    int64_t _cur_index;
    ConnectionID _cur_id;
    std::map<uint64_t, ConnectionID> _ids_map;

    ConnectionIDCB _add_connection_id_cb;
    ConnectionIDCB _retire_connection_id_cb;
};

}
}

#endif