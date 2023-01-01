#include <cstring>
#include "quic/udp/udp_packet_in.h"
#include "quic/packet/packet_decode.h"

namespace quicx {

UdpPacketIn::UdpPacketIn(std::shared_ptr<IBufferRead> buffer):
    _recv_buffer(buffer) {

}

UdpPacketIn::~UdpPacketIn() {

}

bool UdpPacketIn::SetData(char* data, uint32_t size) {
    /*auto write_pair = _recv_buffer->GetWritePair();
    if (write_pair.second - write_pair.first < size) {
        return false;
    }
    
    memcpy(write_pair.first, data, size);
    _recv_buffer->MoveWritePt(size);
    */
    return true;
}

bool UdpPacketIn::Decode(std::vector<std::shared_ptr<IPacket>>& out_packets) {
    return DecodePackets(_recv_buffer, out_packets);
}

}
