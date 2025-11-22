#include "common/log/log.h"
#include "quic/packet/type.h"
#include "quic/packet/header/header_flag.h"
#include "common/buffer/buffer_decode_wrapper.h"
#include "common/buffer/buffer_encode_wrapper.h"

namespace quicx {
namespace quic {

HeaderFlag::HeaderFlag() { 
    flag_.header_flag_ = 0;
    flag_.long_header_flag_.fix_bit_ = 1;
}

HeaderFlag::HeaderFlag(PacketHeaderType type) {
    flag_.header_flag_ = 0;
    flag_.long_header_flag_.header_form_ = type == PacketHeaderType::kShortHeader ? 0 : 1;
    flag_.long_header_flag_.fix_bit_ = 1;
}

HeaderFlag::HeaderFlag(uint8_t flag) {
    flag_.header_flag_ = flag;
}

bool HeaderFlag::EncodeFlag(std::shared_ptr<common::IBuffer> buffer) {
    // Clear buffer before encoding to ensure we write from the beginning
    buffer->Clear();
    
    uint16_t need_size = EncodeFlagSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }
    
    common::BufferEncodeWrapper wrapper(buffer);
    wrapper.EncodeFixedUint8(flag_.header_flag_);
    return true;
}

bool HeaderFlag::DecodeFlag(std::shared_ptr<common::IBuffer> buffer) {
    if (buffer->GetDataLength() < EncodeFlagSize()) {
        return false;
    }

    common::BufferDecodeWrapper wrapper(buffer);
    wrapper.DecodeFixedUint8(flag_.header_flag_);
    return true;
}

uint32_t HeaderFlag::EncodeFlagSize() {
    return sizeof(uint8_t);
}

PacketHeaderType HeaderFlag::GetHeaderType() const {
    return flag_.long_header_flag_.header_form_ == 1 ? PacketHeaderType::kLongHeader : PacketHeaderType::kShortHeader;
}

PacketType HeaderFlag::GetPacketType() {
    if (GetHeaderType() == PacketHeaderType::kShortHeader) {
        return PacketType::k1RttPacketType;
    }
    switch (GetLongHeaderFlag().GetPacketType()) {
    case 0x00:
        return PacketType::kInitialPacketType;
    case 0x01:
        return PacketType::k0RttPacketType;
    case 0x02:
        return PacketType::kHandshakePacketType;
    case 0x03:
        return PacketType::kRetryPacketType;
    default:
        common::LOG_ERROR("unknow packet type. type:%d", GetLongHeaderFlag().packet_type_);
        break;
    }
    return PacketType::kUnknownPacketType;
}

}
}