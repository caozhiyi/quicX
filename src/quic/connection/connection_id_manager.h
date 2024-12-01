#ifndef QUIC_CONNECTION_CONNECTION_ID_MANAGER
#define QUIC_CONNECTION_CONNECTION_ID_MANAGER

#include <map>
#include <string>
#include <cstring>
#include <cstdint>
#include <functional>
#include "quic/connection/type.h"
#include "common/util/singleton.h"

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

    // create a new connection id manager
    // add_connection_id_cb: callback when a new connection id is generated
    // retire_connection_id_cb: callback when a connection id is retired
    ConnectionIDManager(std::function<void(uint64_t/*cid hash*/)> add_connection_id_cb,
                        std::function<void(uint64_t/*cid hash*/)> retire_connection_id_cb):
                        _cur_index(0),
                        _add_connection_id_cb(add_connection_id_cb),
                        _retire_connection_id_cb(retire_connection_id_cb) {}

    ~ConnectionIDManager() {}

    ConnectionID Generator();
    ConnectionID& GetCurrentID();
    bool RetireIDBySequence(uint64_t sequence);
    bool AddID(ConnectionID& id);

private:
    int64_t _cur_index;
    ConnectionID _cur_id;
    std::map<uint64_t, ConnectionID> _ids_map;

    std::function<void(uint64_t/*cid hash*/)> _add_connection_id_cb;
    std::function<void(uint64_t/*cid hash*/)> _retire_connection_id_cb;
};

}
}

#endif