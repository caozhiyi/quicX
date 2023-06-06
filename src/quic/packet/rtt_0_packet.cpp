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

bool Rtt0Packet::Encode(std::shared_ptr<IBufferWrite> buffer, std::shared_ptr<ICryptographer> crypto_grapher) {
    return true;
}

bool Rtt0Packet::Decode(std::shared_ptr<IBufferRead> buffer) {
    return true;
}

bool Rtt0Packet::Decode(std::shared_ptr<ICryptographer> crypto_grapher) {
    return true;
}

bool Rtt0Packet::AddFrame(std::shared_ptr<IFrame> frame) {
    return true;
}

}
