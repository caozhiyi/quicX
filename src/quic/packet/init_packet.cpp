#include "quic/packet/type.h"
#include "quic/packet/init_packet.h"

namespace quicx {

InitPacket::InitPacket() {
    _header_format._header_info._header_form = 1;
    _header_format._header_info._fix_bit = 1;
    _header_format._header_info._packet_type = PT_INITIAL;
}

InitPacket::~InitPacket() {

}

bool InitPacket::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    return true;
}

bool InitPacket::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    return true;
}

uint32_t InitPacket::EncodeSize() {
    return 0;
}

bool InitPacket::AddFrame(std::shared_ptr<Frame> frame) {
    return true;
}

}