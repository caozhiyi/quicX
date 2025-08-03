#ifndef QUIC_QUICX_NEW_UDP_PACKET_LISTENER
#define QUIC_QUICX_NEW_UDP_PACKET_LISTENER

#include <memory>
#include <thread>
#include <atomic>
#include <string>
#include "quic/quicx_new/if_connection_manager.h"
#include "quic/udp/udp_receiver.h"

namespace quicx {
namespace quic {

class UdpPacketListener {
public:
    UdpPacketListener(std::shared_ptr<IConnectionManager> manager);
    ~UdpPacketListener();
    
    void Start();
    void Stop();
    void AddReceiver(const std::string& ip, uint16_t port);
    void AddReceiver(uint64_t socket_fd);

private:
    void ListenLoop();
    void ProcessPacket(std::shared_ptr<INetPacket> packet);
    
    std::shared_ptr<IConnectionManager> connection_manager_;
    std::shared_ptr<UdpReceiver> udp_receiver_;
    std::thread listener_thread_;
    std::atomic<bool> running_;
};

}
}

#endif 