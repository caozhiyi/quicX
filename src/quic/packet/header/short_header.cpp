#include <cstring>
#include "common/log/log.h"
#include "quic/packet/header/short_header.h"
#include "common/buffer/buffer_decode_wrapper.h"
#include "common/buffer/buffer_encode_wrapper.h"

namespace quicx {
namespace quic {

ShortHeader::ShortHeader():
    destination_connection_id_length_(20), // TODO: get from config
    IHeader(PHT_SHORT_HEADER) {

}

ShortHeader::ShortHeader(uint8_t flag):
    destination_connection_id_length_(20), // TODO: get from config
    IHeader(flag) {
}

ShortHeader::~ShortHeader() {

}

bool ShortHeader::EncodeHeader(std::shared_ptr<common::IBufferWrite> buffer) {
    if (!HeaderFlag::EncodeFlag(buffer)) {
        return false;
    }

    uint16_t need_size = EncodeHeaderSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }
    
    common::BufferEncodeWrapper wrapper(buffer);
    if (destination_connection_id_length_ > 0) {
        wrapper.EncodeBytes(destination_connection_id_, destination_connection_id_length_);
    }
    auto data_span = wrapper.GetDataSpan();
    // the header src include header flag
    header_src_data_ = common::BufferSpan(data_span.GetStart() - 1, data_span.GetEnd());
    return true;
}

bool ShortHeader::DecodeHeader(std::shared_ptr<common::IBufferRead> buffer, bool with_flag) {
    if (with_flag) {
        if (!HeaderFlag::DecodeFlag(buffer)) {
            return false;
        }
    }

    // check flag fixed bit
    if (!HeaderFlag::GetLongHeaderFlag().fix_bit_) {
        common::LOG_ERROR("quic fixed bit is not set");
        return false;
    }
    
    common::BufferDecodeWrapper wrapper(buffer);
     if (destination_connection_id_length_ > 0) {
        auto cid = (uint8_t*)destination_connection_id_;
        wrapper.DecodeBytes(cid, destination_connection_id_length_);
    }
    auto data_span = wrapper.GetDataSpan();
    // the header src include header flag
    header_src_data_ = common::BufferSpan(data_span.GetStart() - 1, data_span.GetEnd());
    return true;
}

uint32_t ShortHeader::EncodeHeaderSize() {
    return sizeof(ShortHeader);
}

void ShortHeader::SetDestinationConnectionId(uint8_t* id, uint8_t len) {
    destination_connection_id_length_ = len;
    if (id != nullptr) {
        memcpy(destination_connection_id_, id, len);
    }
}

}
}
