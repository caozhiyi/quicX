
#include "quic/packet/type.h"
#include "quic/packet/retry_packet.h"

namespace quicx {

RetryPacket::RetryPacket(std::shared_ptr<IHeader> header):
    IPacket(header) {

}

RetryPacket::~RetryPacket() {

}

bool RetryPacket::Encode(std::shared_ptr<IBufferWriteOnly> buffer) {
    return true;
}

bool RetryPacket::Decode(std::shared_ptr<IBufferReadOnly> buffer) {
    return true;
}

uint32_t RetryPacket::EncodeSize() {
    return 0;
}

bool RetryPacket::AddFrame(std::shared_ptr<IFrame> frame) {
    return true;
}

}
