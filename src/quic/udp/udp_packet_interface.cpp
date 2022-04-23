#include "quic/udp/udp_packet_interface.h"

namespace quicx {

IUdpPacket::IUdpPacket() {

}

IUdpPacket::~IUdpPacket() {

}

void IUdpPacket::SetPeerAddress(std::shared_ptr<Address> peer_addr) {
    _peer_addr = peer_addr;
}

std::shared_ptr<Address> IUdpPacket::GetPeerAddress() {
    return _peer_addr;
}

}
