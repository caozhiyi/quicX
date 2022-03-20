
#include "quic/packet/type.h"
#include "quic/packet/retry_packet.h"

namespace quicx {

RetryPacket::RetryPacket() {
    _header_format._header_info._header_form = 1;
    _header_format._header_info._fix_bit = 1;
    _header_format._header_info._packet_type = PT_RETRY;
}

RetryPacket::~RetryPacket() {

}

bool RetryPacket::Encode(std::shared_ptr<IBufferWriteOnly> buffer) {
    return true;
}

bool RetryPacket::Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type) {
    return true;
}

uint32_t RetryPacket::EncodeSize() {
    return 0;
}

bool RetryPacket::AddFrame(std::shared_ptr<IFrame> frame) {
    return true;
}

}
