#ifndef QUIC_QUICX_NEW_CONNECTION_EVENT
#define QUIC_QUICX_NEW_CONNECTION_EVENT

#include <memory>
#include <chrono>
#include <thread>
#include "quic/connection/if_connection.h"

namespace quicx {
namespace quic {

struct ConnectionEvent {
    enum class EventType {
        ADD_CONNECTION_ID,     // Add new connection ID mapping
        REMOVE_CONNECTION_ID,  // Remove connection ID mapping
        CONNECTION_CLOSE,      // Connection closed
        WORKER_REGISTER,       // Worker thread registration
        WORKER_UNREGISTER      // Worker thread unregistration
    };
    
    EventType type;
    uint64_t cid_hash;
    std::thread::id worker_id;
    std::shared_ptr<IConnection> connection;  // Optional, used for connection close events
    std::chrono::steady_clock::time_point timestamp;
    
    ConnectionEvent() : type(EventType::ADD_CONNECTION_ID), cid_hash(0), worker_id(), connection(nullptr) {
        timestamp = std::chrono::steady_clock::now();
    }
    
    ConnectionEvent(EventType t, uint64_t cid, std::thread::id worker) 
        : type(t), cid_hash(cid), worker_id(worker), connection(nullptr) {
        timestamp = std::chrono::steady_clock::now();
    }
};

}
}

#endif 