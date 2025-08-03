#ifndef QUIC_QUICX_NEW_CONNECTION_DISPATCHER
#define QUIC_QUICX_NEW_CONNECTION_DISPATCHER


#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <unordered_map>
#include "quic/quicx_new/connection_event.h"

namespace quicx {
namespace quic {

class ConnectionDispatcher {
public:
    ConnectionDispatcher();
    ~ConnectionDispatcher();
    
    // Find worker thread for connection ID
    std::thread::id FindWorker(uint64_t cid_hash);
    
    // Process connection event - lock-free operation
    void ProcessConnectionEvent(std::shared_ptr<ConnectionEvent> event);
    
    // Allocate worker thread for new connection (load balancing)
    std::thread::id AllocateWorker();
    
    // Get current mapping state (for debugging)
    std::unordered_map<uint64_t, std::thread::id> GetConnectionMap() const;
    
    // Register worker thread
    void RegisterWorker(std::thread::id worker_id);
    
    // Unregister worker thread
    void UnregisterWorker(std::thread::id worker_id);
    
    // Get available workers count
    size_t GetAvailableWorkerCount() const;

private:
    std::atomic<size_t> next_worker_index_;
    std::vector<std::thread::id> available_workers_;
    std::unordered_map<uint64_t, std::thread::id> cid_to_worker_map_;

    // Note: No mutex needed as all modifications are processed serially through event queue
};

}
}

#endif 