#include "quic/packet/version_negotiation_packet.h"

namespace quicx {

VersionNegotiationPacket::VersionNegotiationPacket(std::shared_ptr<IHeader> header):
    IPacket(header) {

}

VersionNegotiationPacket::~VersionNegotiationPacket() {
    
}

bool VersionNegotiationPacket::Encode(std::shared_ptr<IBufferWriteOnly> buffer) {
    
    return true;
}

bool VersionNegotiationPacket::Decode(std::shared_ptr<IBufferReadOnly> buffer) {

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
