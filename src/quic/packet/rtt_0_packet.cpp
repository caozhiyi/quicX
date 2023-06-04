#include "quic/packet/type.h"
#include "quic/packet/rtt_0_packet.h"

namespace quicx {

Rtt0Packet::Rtt0Packet() {
    _header.GetLongHeaderFlag().SetPacketType(PT_0RTT);
}

Rtt0Packet::Rtt0Packet(uint8_t flag):
    _header(flag) {

}

Rtt0Packet::~Rtt0Packet() {

}

bool Rtt0Packet::Encode(std::shared_ptr<IBufferWrite> buffer) {
    return true;
}

bool Rtt0Packet::Decode(std::shared_ptr<IBufferRead> buffer) {
    return true;
}

uint32_t Rtt0Packet::EncodeSize() {
    return 0;
}

bool Rtt0Packet::AddFrame(std::shared_ptr<IFrame> frame) {
    return true;
}

}
