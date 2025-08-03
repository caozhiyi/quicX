#ifndef QUIC_QUICX_NEW_SINGLE_THREAD_CONNECTION_MANAGER
#define QUIC_QUICX_NEW_SINGLE_THREAD_CONNECTION_MANAGER

#include <memory>
#include <thread>
#include <atomic>
#include "quic/quicx_new/if_connection_manager.h"
#include "quic/quicx_new/connection_dispatcher.h"
#include "quic/quicx_new/connection_event_processor.h"
#include "quic/quicx_new/udp_packet_listener.h"

namespace quicx {
namespace quic {

class SingleThreadConnectionManager : public IConnectionManager {
public:
    SingleThreadConnectionManager();
    ~SingleThreadConnectionManager();
    
    void HandlePacket(std::shared_ptr<INetPacket> packet) override;
    void RegisterWorker(std::thread::id worker_id, 
                       std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<PacketTask>>> queue) override;
    void UnregisterWorker(std::thread::id worker_id) override;
    std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<ConnectionEvent>>> GetEventQueue() override;
    size_t GetWorkerCount() const override;
    void Start() override;
    void Stop() override;

private:
    void ProcessingLoop();
    void ProcessPacketTask(std::shared_ptr<PacketTask> task);
    uint64_t ExtractConnectionId(std::shared_ptr<INetPacket> packet);
    
    std::shared_ptr<ConnectionDispatcher> dispatcher_;
    std::shared_ptr<ConnectionEventProcessor> event_processor_;
    std::shared_ptr<UdpPacketListener> udp_listener_;
    std::shared_ptr<common::ThreadSafeBlockQueue<std::shared_ptr<PacketTask>>> single_queue_;
    std::thread processing_thread_;
    std::atomic<bool> running_;
    
    // Single worker thread ID
    std::thread::id single_worker_id_;
};

}
}

#endif 