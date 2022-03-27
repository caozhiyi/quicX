#include "quic/packet/type.h"
#include "quic/packet/init_packet.h"

namespace quicx {

InitPacket::InitPacket() {

}

InitPacket::InitPacket(std::shared_ptr<IHeader> header):
    IPacket(header) {

}

InitPacket::~InitPacket() {

}

bool InitPacket::Encode(std::shared_ptr<IBufferWriteOnly> buffer) {
    return true;
}

bool InitPacket::Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_header) {
    return true;
}

uint32_t InitPacket::EncodeSize() {
    return 0;
}

bool InitPacket::AddFrame(std::shared_ptr<IFrame> frame) {
    return true;
}

}