#include "quic/quicx_new/simplified_worker_manager.h"
#include "common/log/log.h"

namespace quicx {
namespace quic {

SimplifiedWorkerManager::SimplifiedWorkerManager(std::shared_ptr<IConnectionManager> manager, size_t worker_count)
    : connection_manager_(manager), running_(false), worker_count_(worker_count) {
}

SimplifiedWorkerManager::~SimplifiedWorkerManager() {
    Stop();
}

void SimplifiedWorkerManager::Start() {
    if (running_.load()) {
        return;
    }
    
    running_.store(true);
    
    // Create all worker threads at once
    for (size_t i = 0; i < worker_count_; ++i) {
        auto queue = std::make_shared<common::ThreadSafeBlockQueue<std::shared_ptr<PacketTask>>>();
        auto worker_info = std::make_shared<WorkerInfo>(std::thread::id(), queue);
        workers_.push_back(worker_info);
        
        // Start worker thread
        worker_threads_.emplace_back(&SimplifiedWorkerManager::WorkerLoop, this, std::thread::id());
    }
}

void SimplifiedWorkerManager::Stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // Wait for all worker threads to finish
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    worker_threads_.clear();
    workers_.clear();
}

std::vector<std::shared_ptr<WorkerInfo>> SimplifiedWorkerManager::GetWorkers() const {
    return workers_;
}

size_t SimplifiedWorkerManager::GetWorkerCount() const {
    return workers_.size();
}

void SimplifiedWorkerManager::WorkerLoop(std::thread::id worker_id) {
    // Set the actual thread ID
    worker_id = std::this_thread::get_id();
    
    // Find our worker info
    std::shared_ptr<WorkerInfo> worker_info = nullptr;
    for (auto& worker : workers_) {
        if (worker->worker_id == std::thread::id()) {
            worker->worker_id = worker_id;
            worker_info = worker;
            break;
        }
    }
    
    if (!worker_info || !worker_info->queue) {
        return;
    }
    
    // Register with connection manager
    if (connection_manager_) {
        connection_manager_->RegisterWorker(worker_id, worker_info->queue);
    }
    
    while (running_.load()) {
        std::shared_ptr<PacketTask> task;
        if (worker_info->queue->TryPop(task, std::chrono::milliseconds(100))) {
            ProcessPacketTask(task);
            worker_info->packet_count.fetch_add(1);
            worker_info->last_active = std::chrono::steady_clock::now();
        }
    }
    
    // Unregister from connection manager
    if (connection_manager_) {
        connection_manager_->UnregisterWorker(worker_id);
    }
}

void SimplifiedWorkerManager::ProcessPacketTask(std::shared_ptr<PacketTask> task) {
    if (!task) {
        return;
    }
    
    switch (task->type) {
        case PacketTask::TaskType::PACKET_DATA:
            // Process regular packet data
            break;
            
        case PacketTask::TaskType::NEW_CONNECTION:
            // Handle new connection
            break;
            
        case PacketTask::TaskType::CONNECTION_CLOSE:
            // Handle connection close
            break;
            
        case PacketTask::TaskType::ADD_CONNECTION_ID:
            // Handle adding connection ID
            break;
            
        case PacketTask::TaskType::REMOVE_CONNECTION_ID:
            // Handle removing connection ID
            break;
            
        default:
            break;
    }
}

}
} 