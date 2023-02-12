#include "quic/udp/udp_packet_interface.h"

namespace quicx {

IUdpPacket::IUdpPacket() {

}

IUdpPacket::~IUdpPacket() {

}

void IUdpPacket::SetPeerAddress(const Address&& addr) {
    _peer_addr = std::move(addr);
}

const Address& IUdpPacket::GetPeerAddress() const {
    return _peer_addr;
}

void IUdpPacket::SetData(const std::shared_ptr<IBuffer>&& buffer) {
    _buffer = std::move(buffer);
}

std::shared_ptr<IBuffer> IUdpPacket::GetBuffer() const {
    return _buffer;
}

}
