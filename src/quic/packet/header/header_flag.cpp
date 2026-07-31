#include "common/buffer/buffer_decode_wrapper.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/log/log.h"

#include "quic/packet/header/header_flag.h"
#include "quic/packet/type.h"

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
    // NOTE: Earlier versions of this method called buffer->Clear() here on
    // the (incorrect) assumption that the flag byte is always the very
    // first thing written into a fresh per-packet buffer. RFC 9000 §12.2
    // packet coalescing breaks that assumption: TryCoalescedInitialHandshake
    // appends a second packet (Handshake) onto the same buffer that already
    // contains the first (Initial). Clearing here would silently wipe the
    // Initial bytes, leaving an Initial-shaped hole that the wire never
    // sees and the peer fails to decode (only the second packet survives).
    //
    // The flag byte must therefore be *appended* at the current write
    // pointer like every other field of the packet. Callers that need a
    // clean buffer must Clear() it explicitly themselves; this method
    // remains append-only to compose correctly under coalescing.
    uint32_t need_size = EncodeFlagSize();
    if (need_size > buffer->GetFreeLength()) {
        LOG_ERROR(
            "insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
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
            LOG_ERROR("unknown packet type. type:%d", GetLongHeaderFlag().packet_type_);
            break;
    }
    return PacketType::kUnknownPacketType;
}

}  // namespace quic
}  // namespace quicx