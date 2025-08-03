#include "quic/quicx_new/simplified_connection_manager.h"
#include "common/log/log.h"

namespace quicx {
namespace quic {

SimplifiedConnectionManager::SimplifiedConnectionManager(size_t worker_count)
    : running_(false), worker_count_(worker_count), next_worker_index_(0) {
    
    udp_receiver_ = std::make_shared<UdpReceiver>();
    event_queue_ = std::make_shared<common::ThreadSafeBlockQueue<std::shared_ptr<ConnectionEvent>>>();
    workers_.reserve(worker_count);
    for (size_t i = 0; i < worker_count; ++i) {
        workers_.push_back(nullptr);
    }
}

SimplifiedConnectionManager::~SimplifiedConnectionManager() {
    Stop();
}

void SimplifiedConnectionManager::HandlePacket(std::shared_ptr<INetPacket> packet) {
    if (!running_.load()) {
        return;
    }
    
    uint64_t cid_hash = ExtractConnectionId(packet);
    if (cid_hash == 0) {
        return;
    }
    
    std::thread::id target_worker = FindWorker(cid_hash);
    
    if (target_worker == std::thread::id()) {
        target_worker = AllocateWorker();
        if (target_worker == std::thread::id()) {
            return;
        }
    }
    
    auto task = std::make_shared<PacketTask>(PacketTask::TaskType::PACKET_DATA, packet, cid_hash, target_worker);
    
    auto it = worker_info_map_.find(target_worker);
    if (it != worker_info_map_.end() && it->second->queue) {
        it->second->queue->Push(task);
    }
}

void SimplifiedConnectionManager::RegisterWorker(std::thread::id worker_id, 
                                               std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<PacketTask>>> queue) {
    auto worker_info = std::make_shared<WorkerInfo>(worker_id, queue);
    worker_info_map_[worker_id] = worker_info;
    
    for (size_t i = 0; i < workers_.size(); ++i) {
        if (workers_[i] == nullptr) {
            workers_[i] = worker_info;
            break;
        }
    }
}

void SimplifiedConnectionManager::UnregisterWorker(std::thread::id worker_id) {
    worker_info_map_.erase(worker_id);
    
    for (auto& worker : workers_) {
        if (worker && worker->worker_id == worker_id) {
            worker = nullptr;
            break;
        }
    }
}

std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<ConnectionEvent>>> SimplifiedConnectionManager::GetEventQueue() {
    return event_queue_;
}

size_t SimplifiedConnectionManager::GetWorkerCount() const {
    return worker_count_;
}

void SimplifiedConnectionManager::Start() {
    if (running_.load()) {
        return;
    }
    
    running_.store(true);
    main_thread_ = std::thread(&SimplifiedConnectionManager::MainLoop, this);
}

void SimplifiedConnectionManager::Stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    if (udp_receiver_) {
        udp_receiver_->Wakeup();
    }
    
    if (main_thread_.joinable()) {
        main_thread_.join();
    }
    
    worker_info_map_.clear();
    workers_.clear();
}

void SimplifiedConnectionManager::AddReceiver(const std::string& ip, uint16_t port) {
    if (udp_receiver_) {
        udp_receiver_->AddReceiver(ip, port);
    }
}

void SimplifiedConnectionManager::AddReceiver(uint64_t socket_fd) {
    if (udp_receiver_) {
        udp_receiver_->AddReceiver(socket_fd);
    }
}

void SimplifiedConnectionManager::MainLoop() {
    auto packet = std::make_shared<INetPacket>();
    auto buffer = std::make_shared<common::Buffer>(new uint8_t[1500], 1500);
    packet->SetData(buffer);
    
    while (running_.load()) {
        ProcessConnectionEvents();
        ProcessUdpPacket();
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

void SimplifiedConnectionManager::ProcessUdpPacket() {
    auto packet = std::make_shared<INetPacket>();
    auto buffer = std::make_shared<common::Buffer>(new uint8_t[1500], 1500);
    packet->SetData(buffer);
    
    udp_receiver_->TryRecv(packet, 1);
    
    if (packet->GetData()->GetReadLength() > 0) {
        HandlePacket(packet);
        packet->GetData()->Reset();
    }
}

void SimplifiedConnectionManager::ProcessConnectionEvents() {
    std::shared_ptr<ConnectionEvent> event;
    
    while (event_queue_->TryPop(event)) {
        ProcessConnectionEvent(event);
    }
}

void SimplifiedConnectionManager::ProcessConnectionEvent(std::shared_ptr<ConnectionEvent> event) {
    if (!event) {
        return;
    }
    
    switch (event->type) {
        case ConnectionEvent::EventType::ADD_CONNECTION_ID:
            cid_to_worker_map_[event->cid_hash] = event->worker_id;
            break;
            
        case ConnectionEvent::EventType::REMOVE_CONNECTION_ID:
            cid_to_worker_map_.erase(event->cid_hash);
            break;
            
        default:
            break;
    }
}

uint64_t SimplifiedConnectionManager::ExtractConnectionId(std::shared_ptr<INetPacket> packet) {
    if (!packet || !packet->GetData()) {
        return 0;
    }
    
    auto data = packet->GetData();
    auto span = data->GetReadSpan();
    
    if (span.GetLength() < 8) {
        return 0;
    }
    
    uint64_t hash = 0;
    for (int i = 0; i < 8 && i < span.GetLength(); ++i) {
        hash = (hash << 8) | span.GetStart()[i];
    }
    
    return hash;
}

std::thread::id SimplifiedConnectionManager::FindWorker(uint64_t cid_hash) {
    auto it = cid_to_worker_map_.find(cid_hash);
    if (it != cid_to_worker_map_.end()) {
        return it->second;
    }
    return std::thread::id();
}

std::thread::id SimplifiedConnectionManager::AllocateWorker() {
    if (workers_.empty()) {
        return std::thread::id();
    }
    
    size_t attempts = 0;
    while (attempts < workers_.size()) {
        size_t index = next_worker_index_.fetch_add(1) % workers_.size();
        if (workers_[index] != nullptr) {
            return workers_[index]->worker_id;
        }
        attempts++;
    }
    
    return std::thread::id();
}

}
} 