#include "quic/udp/udp_packet_out.h"

namespace quicx {

UdpPacketOut::UdpPacketOut(std::shared_ptr<IBufferWrite> buffer):
    _send_buffer(buffer) {

}

UdpPacketOut::~UdpPacketOut() {

}

bool UdpPacketOut::AddQuicPacket(std::shared_ptr<IPacket> packet) {
    _packets.emplace_back(packet);
    return true;
}

bool UdpPacketOut::Eecode(char*& out_data, uint32_t& out_len) {
    return true;
}

}
