#include "common/buffer/buffer.h"
#include "quic/process/receiver.h"

namespace quicx {

Receiver::Receiver() {
    _alloter = std::make_shared<BlockMemoryPool>(1500, 5);
}

bool Receiver::Listen(const std::string& ip, uint16_t port) {
    return _receiver.Listen(ip, port);
}

void Receiver::SetRecvSocket(uint64_t sock) {
    _receiver.SetRecvSocket(sock);
}

std::shared_ptr<UdpPacketIn> Receiver::DoRecv() {
    std::shared_ptr<UdpPacketIn> udp_packet = std::make_shared<UdpPacketIn>();
    auto buffer = std::make_shared<Buffer>(_alloter);
    udp_packet->SetData(buffer);

    auto ret = _receiver.DoRecv(udp_packet);
    if (ret == UdpReceiver::RR_SUCCESS) {
        return udp_packet;
    }
    return nullptr;
}

}