#ifndef QUIC_CONNECTION_CONNECTION_ID_MANAGER
#define QUIC_CONNECTION_CONNECTION_ID_MANAGER

#include <map>
#include <string>
#include <cstring>
#include <cstdint>
#include <functional>
#include "quic/connection/type.h"
#include "quic/connection/connection_id.h"

namespace quicx {
namespace quic {

class ConnectionIDManager {
public:
     ConnectionIDManager(): cur_sequence_number_(0) {}

    // create a new connection id manager
    // add_connection_id_cb: callback when a new connection id is generated
    // retire_connection_id_cb: callback when a connection id is retired
    ConnectionIDManager(std::function<void(ConnectionID&)> add_connection_id_cb = nullptr,
                        std::function<void(ConnectionID&)> retire_connection_id_cb = nullptr):
                        cur_sequence_number_(0),
                        add_connection_id_cb_(add_connection_id_cb),
                        retire_connection_id_cb_(retire_connection_id_cb) {}

    ~ConnectionIDManager() {}

    ConnectionID Generator();
    ConnectionID& GetCurrentID();
    bool RetireIDBySequence(uint64_t sequence);
    bool AddID(ConnectionID& id);
    bool AddID(const uint8_t* id, uint16_t len);
    bool UseNextID();
    
    // Get the number of available CIDs in the pool
    size_t GetAvailableIDCount() const { return sequence_cid_map_.size(); }

private:
    ConnectionID cur_id_;
    int64_t cur_sequence_number_;
    std::map<uint64_t, ConnectionID> sequence_cid_map_;

    std::function<void(ConnectionID&)> add_connection_id_cb_;
    std::function<void(ConnectionID&)> retire_connection_id_cb_;
};

}
}

#endif