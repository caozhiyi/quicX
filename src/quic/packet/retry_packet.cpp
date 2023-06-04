
#include "quic/packet/type.h"
#include "quic/packet/retry_packet.h"

namespace quicx {

RetryPacket::RetryPacket() {
    _header.GetLongHeaderFlag().SetPacketType(PT_RETRY);
}

RetryPacket::RetryPacket(uint8_t flag):
    _header(flag) {

}

RetryPacket::~RetryPacket() {

}

bool RetryPacket::Encode(std::shared_ptr<IBufferWrite> buffer) {
    return true;
}

bool RetryPacket::Decode(std::shared_ptr<IBufferRead> buffer) {
    return true;
}

uint32_t RetryPacket::EncodeSize() {
    return 0;
}

bool RetryPacket::AddFrame(std::shared_ptr<IFrame> frame) {
    return true;
}

}
