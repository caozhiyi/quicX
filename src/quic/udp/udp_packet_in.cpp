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
    /*auto write_pair = _recv_buffer->GetWriteSpan();
    if (write_pair.second - write_pair.first < size) {
        return false;
    }
    
    memcpy(write_pair.first, data, size);
    _recv_buffer->MoveWritePt(size);
    */
    return true;
}

}
