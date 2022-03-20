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

bool InitPacket::Encode(std::shared_ptr<IBufferWriteOnly> buffer) {
    return true;
}

bool InitPacket::Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type) {
    return true;
}

uint32_t InitPacket::EncodeSize() {
    return 0;
}

bool InitPacket::AddFrame(std::shared_ptr<IFrame> frame) {
    return true;
}

}