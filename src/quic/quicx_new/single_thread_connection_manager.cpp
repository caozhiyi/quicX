#include "quic/quicx_new/single_thread_connection_manager.h"
#include "common/log/log.h"

namespace quicx {
namespace quic {

SingleThreadConnectionManager::SingleThreadConnectionManager()
    : running_(false) {
    
    // Initialize components
    dispatcher_ = std::make_shared<ConnectionDispatcher>();
    single_queue_ = std::make_shared<common::ThreadSafeBlockQueue<std::shared_ptr<PacketTask>>>();
    event_processor_ = std::make_shared<ConnectionEventProcessor>(dispatcher_);
    
    // Create event queue
    auto event_queue = std::make_shared<common::ThreadSafeBlockQueue<std::shared_ptr<ConnectionEvent>>>();
    event_processor_->SetEventQueue(event_queue);
}

SingleThreadConnectionManager::~SingleThreadConnectionManager() {
    Stop();
}

void SingleThreadConnectionManager::HandlePacket(std::shared_ptr<INetPacket> packet) {
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
        
        // In single thread mode, all packets go to the single worker
        auto task = std::make_shared<PacketTask>(PacketTask::TaskType::PACKET_DATA, packet, cid_hash, single_worker_id_);
        single_queue_->Push(task);
        
        common::LOG_DEBUG("Queued packet for CID 0x%lx in single thread mode", cid_hash);
        
    } catch (const std::exception& e) {
        common::LOG_ERROR("Exception while handling packet: %s", e.what());
    }
}

void SingleThreadConnectionManager::RegisterWorker(std::thread::id worker_id, 
                                                 std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<PacketTask>>> queue) {
    // In single thread mode, we only support one worker
    single_worker_id_ = worker_id;
    
    // Send registration event
    auto event_queue = event_processor_->GetEventQueue();
    if (event_queue) {
        auto event = std::make_shared<ConnectionEvent>(ConnectionEvent::EventType::WORKER_REGISTER, 0, worker_id);
        event_queue->Push(event);
    }
    
    common::LOG_DEBUG("Registered single worker thread: %lu", worker_id);
}

void SingleThreadConnectionManager::UnregisterWorker(std::thread::id worker_id) {
    // Send unregistration event
    auto event_queue = event_processor_->GetEventQueue();
    if (event_queue) {
        auto event = std::make_shared<ConnectionEvent>(ConnectionEvent::EventType::WORKER_UNREGISTER, 0, worker_id);
        event_queue->Push(event);
    }
    
    common::LOG_DEBUG("Unregistered single worker thread: %lu", worker_id);
}

std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<ConnectionEvent>>> SingleThreadConnectionManager::GetEventQueue() {
    // This is a bit of a hack - we need to expose the event queue
    // In a real implementation, we'd store it as a member variable
    return nullptr; // TODO: Fix this
}

size_t SingleThreadConnectionManager::GetWorkerCount() const {
    return 1; // Single thread mode always has 1 worker
}

void SingleThreadConnectionManager::Start() {
    if (running_.load()) {
        common::LOG_WARN("SingleThreadConnectionManager is already running");
        return;
    }
    
    running_.store(true);
    
    // Start event processor
    event_processor_->Start();
    
    // Start processing thread
    processing_thread_ = std::thread(&SingleThreadConnectionManager::ProcessingLoop, this);
    
    // Start UDP listener
    udp_listener_ = std::make_shared<UdpPacketListener>(std::shared_ptr<IConnectionManager>(this));
    udp_listener_->Start();
    
    common::LOG_INFO("SingleThreadConnectionManager started");
}

void SingleThreadConnectionManager::Stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // Stop components
    if (udp_listener_) {
        udp_listener_->Stop();
    }
    
    if (event_processor_) {
        event_processor_->Stop();
    }
    
    // Wait for processing thread
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }
    
    common::LOG_INFO("SingleThreadConnectionManager stopped");
}

void SingleThreadConnectionManager::ProcessingLoop() {
    common::LOG_DEBUG("SingleThreadConnectionManager processing loop started");
    
    while (running_.load()) {
        std::shared_ptr<PacketTask> task;
        if (single_queue_->TryPop(task, std::chrono::milliseconds(100))) {
            ProcessPacketTask(task);
        }
    }
    
    common::LOG_DEBUG("SingleThreadConnectionManager processing loop stopped");
}

void SingleThreadConnectionManager::ProcessPacketTask(std::shared_ptr<PacketTask> task) {
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

uint64_t SingleThreadConnectionManager::ExtractConnectionId(std::shared_ptr<INetPacket> packet) {
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