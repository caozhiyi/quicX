#include "quic/packet/version_negotiation_packet.h"

namespace quicx {

VersionNegotiationPacket::VersionNegotiationPacket() {
    _header.GetLongHeaderFlag().SetPacketType(PT_NEGOTIATION);
}

VersionNegotiationPacket::~VersionNegotiationPacket() {
    
}

bool VersionNegotiationPacket::Encode(std::shared_ptr<IBufferWrite> buffer) {
    
    return true;
}

bool VersionNegotiationPacket::Decode(std::shared_ptr<IBufferRead> buffer) {

    return true;
}

uint32_t VersionNegotiationPacket::EncodeSize() {
    return 0;
}

bool VersionNegotiationPacket::AddFrame(std::shared_ptr<IFrame> frame) {
    // do nothing
    return true;
}

}
