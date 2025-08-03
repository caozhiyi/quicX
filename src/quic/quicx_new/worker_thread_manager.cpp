#include "quic/quicx_new/worker_thread_manager.h"
#include "common/log/log.h"

namespace quicx {
namespace quic {

WorkerThreadManager::WorkerThreadManager(std::shared_ptr<IConnectionManager> manager, size_t worker_count)
    : connection_manager_(manager), running_(false), initial_worker_count_(worker_count) {
}

WorkerThreadManager::~WorkerThreadManager() {
    Stop();
}

void WorkerThreadManager::Start() {
    if (running_.load()) {
        common::LOG_WARN("WorkerThreadManager is already running");
        return;
    }
    
    running_.store(true);
    
    // Create initial workers
    for (size_t i = 0; i < initial_worker_count_; ++i) {
        AddWorker();
    }
    
    common::LOG_INFO("WorkerThreadManager started with %zu workers", initial_worker_count_);
}

void WorkerThreadManager::Stop() {
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
    
    common::LOG_INFO("WorkerThreadManager stopped");
}

void WorkerThreadManager::AddWorker() {
    auto worker_id = std::this_thread::get_id(); // This will be set in the worker thread
    auto queue = std::make_shared<common::ThreadSafeBlockQueue<std::shared_ptr<PacketTask>>>();
    auto worker_info = std::make_shared<WorkerInfo>(worker_id, queue);
    
    workers_.push_back(worker_info);
    
    // Start worker thread
    worker_threads_.emplace_back(&WorkerThreadManager::WorkerLoop, this, worker_id);
    
    common::LOG_DEBUG("Added worker thread: %lu", worker_id);
}

void WorkerThreadManager::RemoveWorker() {
    if (workers_.empty()) {
        common::LOG_WARN("No workers to remove");
        return;
    }
    
    // Remove the last worker
    workers_.pop_back();
    
    if (!worker_threads_.empty()) {
        // Note: In a real implementation, you'd need to signal the thread to stop
        // and wait for it to finish before removing
        common::LOG_WARN("Worker removal not fully implemented - thread may still be running");
    }
    
    common::LOG_DEBUG("Removed worker thread");
}

std::vector<std::shared_ptr<WorkerInfo>> WorkerThreadManager::GetWorkers() const {
    return workers_;
}

size_t WorkerThreadManager::GetWorkerCount() const {
    return workers_.size();
}

void WorkerThreadManager::WorkerLoop(std::thread::id worker_id) {
    common::LOG_DEBUG("Worker thread %lu started", worker_id);
    
    // Find our worker info
    std::shared_ptr<WorkerInfo> worker_info = nullptr;
    for (auto& worker : workers_) {
        if (worker->worker_id == worker_id) {
            worker_info = worker;
            break;
        }
    }
    
    if (!worker_info || !worker_info->queue) {
        common::LOG_ERROR("Worker info not found for thread %lu", worker_id);
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
    
    common::LOG_DEBUG("Worker thread %lu stopped", worker_id);
}

void WorkerThreadManager::ProcessPacketTask(std::shared_ptr<PacketTask> task) {
    if (!task) {
        common::LOG_WARN("Received null packet task");
        return;
    }
    
    try {
        switch (task->type) {
            case PacketTask::TaskType::PACKET_DATA:
                // Process regular packet data
                common::LOG_DEBUG("Processing packet data for CID: 0x%lx", task->cid_hash);
                break;
                
            case PacketTask::TaskType::NEW_CONNECTION:
                // Handle new connection
                common::LOG_DEBUG("Processing new connection for CID: 0x%lx", task->cid_hash);
                break;
                
            case PacketTask::TaskType::CONNECTION_CLOSE:
                // Handle connection close
                common::LOG_DEBUG("Processing connection close for CID: 0x%lx", task->cid_hash);
                break;
                
            case PacketTask::TaskType::ADD_CONNECTION_ID:
                // Handle adding connection ID
                common::LOG_DEBUG("Processing add connection ID: 0x%lx", task->cid_hash);
                break;
                
            case PacketTask::TaskType::REMOVE_CONNECTION_ID:
                // Handle removing connection ID
                common::LOG_DEBUG("Processing remove connection ID: 0x%lx", task->cid_hash);
                break;
                
            default:
                common::LOG_WARN("Unknown packet task type: %d", static_cast<int>(task->type));
                break;
        }
    } catch (const std::exception& e) {
        common::LOG_ERROR("Exception while processing packet task: %s", e.what());
    }
}

}
} 