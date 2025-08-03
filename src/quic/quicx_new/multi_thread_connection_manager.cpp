#include "common/log/log.h"
#include "quic/packet/packet_decode.h"
#include "quic/quicx_new/multi_thread_connection_manager.h"

namespace quicx {
namespace quic {

MultiThreadConnectionManager::MultiThreadConnectionManager(size_t worker_count)
    : running_(false), worker_count_(worker_count) {
    
    // Initialize components
    dispatcher_ = std::make_shared<ConnectionDispatcher>();
    event_queue_ = std::make_shared<common::ThreadSafeBlockQueue<std::shared_ptr<ConnectionEvent>>>();
    event_processor_ = std::make_shared<ConnectionEventProcessor>(dispatcher_);
    worker_manager_ = std::make_shared<WorkerThreadManager>(std::shared_ptr<IConnectionManager>(this), worker_count);
    
    // Set event queue for processor
    event_processor_->SetEventQueue(event_queue_);
}

MultiThreadConnectionManager::~MultiThreadConnectionManager() {
    Stop();
}

void MultiThreadConnectionManager::HandlePacket(std::shared_ptr<INetPacket> packet) {
    if (!running_.load()) {
        common::LOG_WARN("Connection manager not running, dropping packet");
        return;
    }
    
    try {
        // Extract connection ID from packet
        uint64_t cid_hash = ExtractConnectionId(packet);
        if (cid_hash == 0) {
            common::LOG_WARN("Failed to extract connection ID from packet");
            return;
        }
        
        // Find target worker thread
        std::thread::id target_worker = dispatcher_->FindWorker(cid_hash);
        
        if (target_worker == std::thread::id()) {
            // New connection, allocate worker
            target_worker = dispatcher_->AllocateWorker();
            if (target_worker == std::thread::id()) {
                common::LOG_ERROR("No available workers for new connection");
                return;
            }
            
            common::LOG_DEBUG("Allocated new connection 0x%lx to worker %lu", cid_hash, target_worker);
        }
        
        // Create packet task
        auto task = std::make_shared<PacketTask>(PacketTask::TaskType::PACKET_DATA, packet, cid_hash, target_worker);
        
        // Find worker queue and send task
        std::lock_guard<std::mutex> lock(worker_info_mutex_);
        auto it = worker_info_map_.find(target_worker);
        if (it != worker_info_map_.end() && it->second->queue) {
            it->second->queue->Push(task);
            common::LOG_DEBUG("Sent packet task to worker %lu for CID 0x%lx", target_worker, cid_hash);
        } else {
            common::LOG_ERROR("Worker %lu not found or queue not available", target_worker);
        }
        
    } catch (const std::exception& e) {
        common::LOG_ERROR("Exception while handling packet: %s", e.what());
    }
}

void MultiThreadConnectionManager::RegisterWorker(std::thread::id worker_id, 
                                                std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<PacketTask>>> queue) {
    std::lock_guard<std::mutex> lock(worker_info_mutex_);
    
    auto worker_info = std::make_shared<WorkerInfo>(worker_id, queue);
    worker_info_map_[worker_id] = worker_info;
    
    // Send registration event
    auto event = std::make_shared<ConnectionEvent>(ConnectionEvent::EventType::WORKER_REGISTER, 0, worker_id);
    event_queue_->Push(event);
    
    common::LOG_DEBUG("Registered worker thread: %lu", worker_id);
}

void MultiThreadConnectionManager::UnregisterWorker(std::thread::id worker_id) {
    std::lock_guard<std::mutex> lock(worker_info_mutex_);
    
    worker_info_map_.erase(worker_id);
    
    // Send unregistration event
    auto event = std::make_shared<ConnectionEvent>(ConnectionEvent::EventType::WORKER_UNREGISTER, 0, worker_id);
    event_queue_->Push(event);
    
    common::LOG_DEBUG("Unregistered worker thread: %lu", worker_id);
}

std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<ConnectionEvent>>> MultiThreadConnectionManager::GetEventQueue() {
    return event_queue_;
}

size_t MultiThreadConnectionManager::GetWorkerCount() const {
    return worker_count_;
}

void MultiThreadConnectionManager::Start() {
    if (running_.load()) {
        common::LOG_WARN("MultiThreadConnectionManager is already running");
        return;
    }
    
    running_.store(true);
    
    // Start event processor
    event_processor_->Start();
    
    // Start worker manager
    worker_manager_->Start();
    
    // Start UDP listener
    udp_listener_ = std::make_shared<UdpPacketListener>(std::shared_ptr<IConnectionManager>(this));
    udp_listener_->Start();
    
    common::LOG_INFO("MultiThreadConnectionManager started with %zu workers", worker_count_);
}

void MultiThreadConnectionManager::Stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // Stop components
    if (udp_listener_) {
        udp_listener_->Stop();
    }
    
    if (worker_manager_) {
        worker_manager_->Stop();
    }
    
    if (event_processor_) {
        event_processor_->Stop();
    }
    
    // Clear worker info
    std::lock_guard<std::mutex> lock(worker_info_mutex_);
    worker_info_map_.clear();
    
    common::LOG_INFO("MultiThreadConnectionManager stopped");
}

void MultiThreadConnectionManager::EventProcessingLoop() {
    // This is handled by ConnectionEventProcessor
}

void MultiThreadConnectionManager::ProcessConnectionEvent(std::shared_ptr<ConnectionEvent> event) {
    // This is handled by ConnectionEventProcessor
}

uint64_t MultiThreadConnectionManager::ExtractConnectionId(std::shared_ptr<INetPacket> packet) {
    if (!packet || !packet->GetData()) {
        return 0;
    }
    
    try {
        // Simple hash of the first few bytes as connection ID
        // In a real implementation, you'd parse the QUIC packet header properly
        auto data = packet->GetData();
        auto span = data->GetReadSpan();
        
        if (span.GetLength() < 8) {
            return 0;
        }
        
        // Simple hash function for demonstration
        uint64_t hash = 0;
        for (int i = 0; i < 8 && i < span.GetLength(); ++i) {
            hash = (hash << 8) | span.GetStart()[i];
        }
        
        return hash;
        
    } catch (const std::exception& e) {
        common::LOG_ERROR("Exception while extracting connection ID: %s", e.what());
        return 0;
    }
}

}
} 