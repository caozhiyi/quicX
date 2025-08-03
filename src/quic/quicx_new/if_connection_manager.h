#ifndef QUIC_QUICX_NEW_IF_CONNECTION_MANAGER
#define QUIC_QUICX_NEW_IF_CONNECTION_MANAGER

#include <memory>
#include <thread>
#include "common/structure/thread_safe_block_queue.h"
#include "quic/quicx_new/packet_task.h"
#include "quic/quicx_new/connection_event.h"

namespace quicx {
namespace quic {

class IConnectionManager {
public:
    virtual ~IConnectionManager() = default;
    
    // Handle received packet
    virtual void HandlePacket(std::shared_ptr<INetPacket> packet) = 0;
    
    // Register worker thread
    virtual void RegisterWorker(std::thread::id worker_id, 
                               std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<PacketTask>>> queue) = 0;
    
    // Unregister worker thread
    virtual void UnregisterWorker(std::thread::id worker_id) = 0;
    
    // Get event queue - Worker threads send connection change events through this queue
    virtual std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<ConnectionEvent>>> GetEventQueue() = 0;
    
    // Get worker thread count
    virtual size_t GetWorkerCount() const = 0;
    
    // Start connection manager
    virtual void Start() = 0;
    
    // Stop connection manager
    virtual void Stop() = 0;
};

}
}

#endif 