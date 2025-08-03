#include "common/log/log.h"
#include "quic/quicx_new/connection_dispatcher.h"


namespace quicx {
namespace quic {

ConnectionDispatcher::ConnectionDispatcher() : next_worker_index_(0) {
}

ConnectionDispatcher::~ConnectionDispatcher() {
}

std::thread::id ConnectionDispatcher::FindWorker(uint64_t cid_hash) {
    auto it = cid_to_worker_map_.find(cid_hash);
    if (it != cid_to_worker_map_.end()) {
        return it->second;
    }
    return std::thread::id(); // Return invalid thread ID if not found
}

void ConnectionDispatcher::ProcessConnectionEvent(std::shared_ptr<ConnectionEvent> event) {
    switch (event->type) {
        case ConnectionEvent::EventType::ADD_CONNECTION_ID:
            cid_to_worker_map_[event->cid_hash] = event->worker_id;
            common::LOG_DEBUG("Added connection ID mapping: 0x%lx -> worker %lu", 
                     event->cid_hash, event->worker_id);
            break;
            
        case ConnectionEvent::EventType::REMOVE_CONNECTION_ID:
            cid_to_worker_map_.erase(event->cid_hash);
            common::LOG_DEBUG("Removed connection ID mapping: 0x%lx", event->cid_hash);
            break;
            
        case ConnectionEvent::EventType::WORKER_REGISTER:
            available_workers_.push_back(event->worker_id);
            common::LOG_DEBUG("Registered worker thread: %lu", event->worker_id);
            break;
            
        case ConnectionEvent::EventType::WORKER_UNREGISTER:
            available_workers_.erase(
                std::remove(available_workers_.begin(), available_workers_.end(), event->worker_id),
                available_workers_.end()
            );
            common::LOG_DEBUG("Unregistered worker thread: %lu", event->worker_id);
            break;
            
        default:
            common::LOG_WARN("Unknown connection event type: %d", static_cast<int>(event->type));
            break;
    }
}

std::thread::id ConnectionDispatcher::AllocateWorker() {
    if (available_workers_.empty()) {
        common::LOG_ERROR("No available workers for allocation");
        return std::thread::id();
    }
    
    // Round-robin allocation
    size_t index = next_worker_index_.fetch_add(1) % available_workers_.size();
    return available_workers_[index];
}

std::unordered_map<uint64_t, std::thread::id> ConnectionDispatcher::GetConnectionMap() const {
    return cid_to_worker_map_;
}

void ConnectionDispatcher::RegisterWorker(std::thread::id worker_id) {
    available_workers_.push_back(worker_id);
}

void ConnectionDispatcher::UnregisterWorker(std::thread::id worker_id) {
    available_workers_.erase(
        std::remove(available_workers_.begin(), available_workers_.end(), worker_id),
        available_workers_.end()
    );
}

size_t ConnectionDispatcher::GetAvailableWorkerCount() const {
    return available_workers_.size();
}

}
} 