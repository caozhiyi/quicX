#include <cstring>

#include "common/buffer/buffer_decode_wrapper.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/log/log.h"

#include "quic/packet/header/short_header.h"
#include "quic/config.h"

namespace quicx {
namespace quic {

ShortHeader::ShortHeader():
    destination_connection_id_length_(kDefaultDestinationCidLength),
    IHeader(PacketHeaderType::kShortHeader) {}

ShortHeader::ShortHeader(uint8_t flag):
    destination_connection_id_length_(kDefaultDestinationCidLength),
    IHeader(flag) {}

ShortHeader::~ShortHeader() {}

bool ShortHeader::EncodeHeader(std::shared_ptr<common::IBuffer> buffer) {
    if (!HeaderFlag::EncodeFlag(buffer)) {
        return false;
    }

    uint16_t need_size = EncodeHeaderSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR(
            "insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    if (destination_connection_id_length_ > 0) {
        wrapper.EncodeBytes(destination_connection_id_, destination_connection_id_length_);
    }
    auto data_span = wrapper.GetDataSpan();
    // the header src include header flag
    header_src_data_ = common::SharedBufferSpan(buffer->GetChunk(), data_span.GetStart() - 1, data_span.GetEnd());
    return true;
}

bool ShortHeader::DecodeHeader(std::shared_ptr<common::IBuffer> buffer, bool with_flag) {
    if (with_flag) {
        if (!HeaderFlag::DecodeFlag(buffer)) {
            return false;
        }
    }

    // check flag fixed bit
    if (!HeaderFlag::GetShortHeaderFlag().fix_bit_) {
        common::LOG_ERROR("quic fixed bit is not set");
        return false;
    }

    common::BufferDecodeWrapper wrapper(buffer);
    if (destination_connection_id_length_ > 0) {
        auto cid = (uint8_t*)destination_connection_id_;
        wrapper.DecodeBytes(cid, destination_connection_id_length_);
    }
    auto data_span = wrapper.GetDataSpan();
    // the header src must include header flag for AEAD AD construction
    // When with_flag=false, the flag was already decoded separately, but we need
    // to ensure it's in the buffer for AD. Write flag byte before the DCID.
    uint8_t* header_start = data_span.GetStart() - 1;
    *header_start = flag_.header_flag_;  // Write the flag byte to ensure it's correct
    header_src_data_ = common::SharedBufferSpan(buffer->GetChunk(), header_start, data_span.GetEnd());
    return true;
}

uint32_t ShortHeader::EncodeHeaderSize() {
    return sizeof(ShortHeader);
}

void ShortHeader::SetDestinationConnectionId(const uint8_t* id, uint8_t len) {
    destination_connection_id_length_ = len;
    if (id != nullptr) {
        memcpy(destination_connection_id_, id, len);
    }
}

}  // namespace quic
}  // namespace quicx
