
#include "quic/packet/type.h"
#include "quic/packet/rtt_1_packet.h"

namespace quicx {

Rtt1Packet::Rtt1Packet() {
    _header_format._header_info._header_form = 0;
    _header_format._header_info._fix_bit = 1;
}

Rtt1Packet::~Rtt1Packet() {

}

bool Rtt1Packet::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    return true;
}

bool Rtt1Packet::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    return true;
}

uint32_t Rtt1Packet::EncodeSize() {
    return 0;
}

bool Rtt1Packet::AddFrame(std::shared_ptr<Frame> frame) {
    return true;
}

}
