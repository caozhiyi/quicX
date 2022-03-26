
#include "quic/packet/type.h"
#include "quic/packet/rtt_1_packet.h"

namespace quicx {

Rtt1Packet::Rtt1Packet() {

}

Rtt1Packet::Rtt1Packet(std::shared_ptr<IHeader> header):
    IPacket(header) {

}

Rtt1Packet::~Rtt1Packet() {

}

bool Rtt1Packet::Encode(std::shared_ptr<IBufferWriteOnly> buffer) {
    return true;
}

bool Rtt1Packet::Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type) {
    return true;
}

uint32_t Rtt1Packet::EncodeSize() {
    return 0;
}

bool Rtt1Packet::AddFrame(std::shared_ptr<IFrame> frame) {
    return true;
}

}
