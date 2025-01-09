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
    uint8_t id_[__max_cid_length];
    uint8_t len_;
    uint64_t index_;
    uint64_t hash_;
    ConnectionID(uint64_t index = 0): len_(__max_cid_length), index_(index), hash_(0) {
        memset(id_, 0, __max_cid_length);
    }
    ConnectionID(uint8_t* id, uint8_t len, uint64_t index = 0): len_(len), index_(index), hash_(0) {
        memset(id_, 0, __max_cid_length);
        memcpy(id_, id, len);
    }
    ~ConnectionID() {}
    uint64_t Hash();
};

class ConnectionIDManager {
public:
     ConnectionIDManager(): cur_index_(0) {}

    // create a new connection id manager
    // add_connection_id_cb: callback when a new connection id is generated
    // retire_connection_id_cb: callback when a connection id is retired
    ConnectionIDManager(std::function<void(uint64_t/*cid hash*/)> add_connection_id_cb,
                        std::function<void(uint64_t/*cid hash*/)> retire_connection_id_cb):
                        cur_index_(0),
                        add_connection_id_cb_(add_connection_id_cb),
                        retire_connection_id_cb_(retire_connection_id_cb) {}

    ~ConnectionIDManager() {}

    ConnectionID Generator();
    ConnectionID& GetCurrentID();
    bool RetireIDBySequence(uint64_t sequence);
    bool AddID(ConnectionID& id);

private:
    int64_t cur_index_;
    ConnectionID cur_id_;
    std::map<uint64_t, ConnectionID> ids_map_;

    std::function<void(uint64_t/*cid hash*/)> add_connection_id_cb_;
    std::function<void(uint64_t/*cid hash*/)> retire_connection_id_cb_;
};

}
}

#endif