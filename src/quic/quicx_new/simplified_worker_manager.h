#ifndef QUIC_QUICX_NEW_SIMPLIFIED_WORKER_MANAGER
#define QUIC_QUICX_NEW_SIMPLIFIED_WORKER_MANAGER

#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include "quic/quicx_new/if_connection_manager.h"
#include "quic/quicx_new/worker_info.h"

namespace quicx {
namespace quic {

class SimplifiedWorkerManager {
public:
    SimplifiedWorkerManager(std::shared_ptr<IConnectionManager> manager, size_t worker_count);
    ~SimplifiedWorkerManager();
    
    void Start();
    void Stop();
    
    // Get worker information
    std::vector<std::shared_ptr<WorkerInfo>> GetWorkers() const;
    size_t GetWorkerCount() const;

private:
    void WorkerLoop(std::thread::id worker_id);
    void ProcessPacketTask(std::shared_ptr<PacketTask> task);
    
    std::shared_ptr<IConnectionManager> connection_manager_;
    std::vector<std::shared_ptr<WorkerInfo>> workers_;
    std::vector<std::thread> worker_threads_;
    std::atomic<bool> running_;
    size_t worker_count_;
};

}
}

#endif 