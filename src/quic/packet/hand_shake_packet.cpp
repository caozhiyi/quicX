
#include "quic/packet/type.h"
#include "quic/packet/hand_shake_packet.h"

namespace quicx {

HandShakePacket::HandShakePacket() {

}

HandShakePacket::HandShakePacket(std::shared_ptr<IHeader> header):
    IPacket(header) {

}

HandShakePacket::~HandShakePacket() {

}

bool HandShakePacket::Encode(std::shared_ptr<IBufferWriteOnly> buffer) {
    return true;
}

bool HandShakePacket::Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_header) {
    return true;
}

uint32_t HandShakePacket::EncodeSize() {
    return 0;
}

bool HandShakePacket::AddFrame(std::shared_ptr<IFrame> frame) {
    return true;
}

}