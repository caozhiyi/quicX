#ifndef QUIC_QUICX_NEW_SIMPLIFIED_CONNECTION_MANAGER
#define QUIC_QUICX_NEW_SIMPLIFIED_CONNECTION_MANAGER

#include <memory>
#include <unordered_map>
#include <vector>
#include <thread>
#include <atomic>
#include "quic/quicx_new/if_connection_manager.h"
#include "quic/quicx_new/connection_event.h"
#include "quic/quicx_new/worker_info.h"
#include "quic/udp/udp_receiver.h"

namespace quicx {
namespace quic {

class SimplifiedConnectionManager : public IConnectionManager {
public:
    SimplifiedConnectionManager(size_t worker_count = 4);
    ~SimplifiedConnectionManager();
    
    void HandlePacket(std::shared_ptr<INetPacket> packet) override;
    void RegisterWorker(std::thread::id worker_id, 
                       std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<PacketTask>>> queue) override;
    void UnregisterWorker(std::thread::id worker_id) override;
    std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<ConnectionEvent>>> GetEventQueue() override;
    size_t GetWorkerCount() const override;
    void Start() override;
    void Stop() override;
    
    // Add UDP receiver
    void AddReceiver(const std::string& ip, uint16_t port);
    void AddReceiver(uint64_t socket_fd);

private:
    void MainLoop();
    void ProcessUdpPacket();
    void ProcessConnectionEvents();
    void ProcessConnectionEvent(std::shared_ptr<ConnectionEvent> event);
    uint64_t ExtractConnectionId(std::shared_ptr<INetPacket> packet);
    std::thread::id FindWorker(uint64_t cid_hash);
    std::thread::id AllocateWorker();
    
    // Components
    std::shared_ptr<UdpReceiver> udp_receiver_;
    std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<ConnectionEvent>>> event_queue_;
    
    // Worker management
    std::vector<std::shared_ptr<WorkerInfo>> workers_;
    std::unordered_map<std::thread::id, std::shared_ptr<WorkerInfo>> worker_info_map_;
    
    // Connection ID mapping
    std::unordered_map<uint64_t, std::thread::id> cid_to_worker_map_;
    std::atomic<size_t> next_worker_index_;
    
    // Thread management
    std::thread main_thread_;
    std::atomic<bool> running_;
    size_t worker_count_;
};

}
}

#endif 