
#include "quic/packet/type.h"
#include "quic/packet/hand_shake_packet.h"

namespace quicx {

HandShakePacket::HandShakePacket() {
    _header_format._header_info._header_form = 1;
    _header_format._header_info._fix_bit = 1;
    _header_format._header_info._packet_type = PT_HANDSHAKE;
}

HandShakePacket::~HandShakePacket() {

}

bool HandShakePacket::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    return true;
}

bool HandShakePacket::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    return true;
}

uint32_t HandShakePacket::EncodeSize() {
    return 0;
}

bool HandShakePacket::AddFrame(std::shared_ptr<Frame> frame) {
    return true;
}

}