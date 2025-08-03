#ifndef QUIC_QUICX_NEW_MULTI_THREAD_CONNECTION_MANAGER
#define QUIC_QUICX_NEW_MULTI_THREAD_CONNECTION_MANAGER

#include <mutex>
#include <memory>
#include <unordered_map>

#include "quic/quicx_new/worker_info.h"
#include "quic/quicx_new/udp_packet_listener.h"
#include "quic/quicx_new/worker_thread_manager.h"
#include "quic/quicx_new/if_connection_manager.h"
#include "quic/quicx_new/connection_dispatcher.h"
#include "quic/quicx_new/connection_event_processor.h"

namespace quicx {
namespace quic {

class MultiThreadConnectionManager:
    public IConnectionManager {
public:
    MultiThreadConnectionManager(size_t worker_count = 4);
    ~MultiThreadConnectionManager();
    
    void HandlePacket(std::shared_ptr<INetPacket> packet) override;
    void RegisterWorker(std::thread::id worker_id, 
                       std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<PacketTask>>> queue) override;
    void UnregisterWorker(std::thread::id worker_id) override;
    std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<ConnectionEvent>>> GetEventQueue() override;
    size_t GetWorkerCount() const override;
    void Start() override;
    void Stop() override;

private:
    void EventProcessingLoop();
    void ProcessConnectionEvent(std::shared_ptr<ConnectionEvent> event);
    uint64_t ExtractConnectionId(std::shared_ptr<INetPacket> packet);
    
    std::shared_ptr<ConnectionDispatcher> dispatcher_;
    std::shared_ptr<ConnectionEventProcessor> event_processor_;
    std::shared_ptr<UdpPacketListener> udp_listener_;
    std::shared_ptr<WorkerThreadManager> worker_manager_;
    
    // Connection event queue
    std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<ConnectionEvent>>> event_queue_;
    std::thread event_processing_thread_;
    
    // Worker information management - updated through event queue, no locks needed
    std::unordered_map<std::thread::id, std::shared_ptr<WorkerInfo>> worker_info_map_;
    std::mutex worker_info_mutex_;
    
    std::atomic<bool> running_;
    size_t worker_count_;
};

}
}

#endif 