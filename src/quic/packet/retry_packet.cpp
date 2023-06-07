
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

bool RetryPacket::Encode(std::shared_ptr<IBufferWrite> buffer, std::shared_ptr<ICryptographer> crypto_grapher) {
    return true;
}

bool RetryPacket::Decode(std::shared_ptr<IBufferRead> buffer) {
    return true;
}

bool RetryPacket::Decode(std::shared_ptr<ICryptographer> crypto_grapher, std::shared_ptr<IBuffer> buffer) {
    return true;
}

bool RetryPacket::AddFrame(std::shared_ptr<IFrame> frame) {
    return true;
}

}
