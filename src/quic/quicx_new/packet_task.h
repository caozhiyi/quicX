#ifndef QUIC_QUICX_NEW_PACKET_TASK
#define QUIC_QUICX_NEW_PACKET_TASK

#include <memory>
#include <thread>
#include "quic/quicx/if_net_packet.h"
#include "quic/connection/if_connection.h"

namespace quicx {
namespace quic {

struct PacketTask {
    enum class TaskType {
        PACKET_DATA,           // Regular data packet
        NEW_CONNECTION,        // New connection
        CONNECTION_CLOSE,      // Connection close
        ADD_CONNECTION_ID,     // Add connection ID
        REMOVE_CONNECTION_ID   // Remove connection ID
    };
    
    TaskType type;
    std::shared_ptr<INetPacket> packet;
    uint64_t cid_hash;
    std::thread::id target_worker;  // Target worker thread
    std::shared_ptr<IConnection> connection;  // Associated connection object
    
    PacketTask() : type(TaskType::PACKET_DATA), packet(nullptr), cid_hash(0), 
                   target_worker(), connection(nullptr) {}
    
    PacketTask(TaskType t, std::shared_ptr<INetPacket> pkt, uint64_t cid, std::thread::id worker)
        : type(t), packet(pkt), cid_hash(cid), target_worker(worker), connection(nullptr) {}
};

}
}

#endif 