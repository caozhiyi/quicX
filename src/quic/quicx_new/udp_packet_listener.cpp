#include "quic/quicx_new/udp_packet_listener.h"
#include "quic/quicx/if_net_packet.h"
#include "common/log/log.h"

namespace quicx {
namespace quic {

UdpPacketListener::UdpPacketListener(std::shared_ptr<IConnectionManager> manager)
    : connection_manager_(manager), udp_receiver_(std::make_shared<UdpReceiver>()), running_(false) {
}

UdpPacketListener::~UdpPacketListener() {
    Stop();
}

void UdpPacketListener::Start() {
    if (running_.load()) {
        common::LOG_WARN("UdpPacketListener is already running");
        return;
    }
    
    running_.store(true);
    listener_thread_ = std::thread(&UdpPacketListener::ListenLoop, this);
    common::LOG_INFO("UdpPacketListener started");
}

void UdpPacketListener::Stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    if (udp_receiver_) {
        udp_receiver_->Wakeup();
    }
    
    if (listener_thread_.joinable()) {
        listener_thread_.join();
    }
    
    common::LOG_INFO("UdpPacketListener stopped");
}

void UdpPacketListener::AddReceiver(const std::string& ip, uint16_t port) {
    if (udp_receiver_) {
        udp_receiver_->AddReceiver(ip, port);
        common::LOG_INFO("Added UDP receiver: %s:%d", ip.c_str(), port);
    }
}

void UdpPacketListener::AddReceiver(uint64_t socket_fd) {
    if (udp_receiver_) {
        udp_receiver_->AddReceiver(socket_fd);
        common::LOG_INFO("Added UDP receiver with socket: %lu", socket_fd);
    }
}

void UdpPacketListener::ListenLoop() {
    common::LOG_DEBUG("UdpPacketListener loop started");
    
    auto packet = std::make_shared<INetPacket>();
    auto buffer = std::make_shared<common::Buffer>(new uint8_t[1500], 1500);
    packet->SetData(buffer);
    
    while (running_.load()) {
        try {
            udp_receiver_->TryRecv(packet, 100); // 100ms timeout
            
            if (packet->GetData()->GetReadLength() > 0) {
                ProcessPacket(packet);
                
                // Reset packet for next use
                packet->GetData()->Reset();
            }
        } catch (const std::exception& e) {
            common::LOG_ERROR("Exception in UDP listener loop: %s", e.what());
        }
    }
    
    common::LOG_DEBUG("UdpPacketListener loop stopped");
}

void UdpPacketListener::ProcessPacket(std::shared_ptr<INetPacket> packet) {
    if (!connection_manager_) {
        common::LOG_ERROR("Connection manager not available");
        return;
    }
    
    try {
        connection_manager_->HandlePacket(packet);
    } catch (const std::exception& e) {
        common::LOG_ERROR("Exception while processing packet: %s", e.what());
    }
}

}
} 