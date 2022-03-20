#include "quic/packet/type.h"
#include "quic/packet/rtt_0_packet.h"

namespace quicx {

Rtt0Packet::Rtt0Packet() {
    _header_format._header_info._header_form = 1;
    _header_format._header_info._fix_bit = 1;
    _header_format._header_info._packet_type = PT_0RTT;
}

Rtt0Packet::~Rtt0Packet() {

}

bool Rtt0Packet::Encode(std::shared_ptr<IBufferWriteOnly> buffer) {
    return true;
}

bool Rtt0Packet::Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type) {
    return true;
}

uint32_t Rtt0Packet::EncodeSize() {
    return 0;
}

bool Rtt0Packet::AddFrame(std::shared_ptr<IFrame> frame) {
    return true;
}

}
