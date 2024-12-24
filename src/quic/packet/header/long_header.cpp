#include <memory>
#include <cstring>

#include "common/log/log.h"
#include "quic/common/constants.h"
#include "common/alloter/if_alloter.h"
#include "quic/packet/header/long_header.h"
#include "common/buffer/buffer_decode_wrapper.h"
#include "common/buffer/buffer_encode_wrapper.h"

namespace quicx {
namespace quic {

LongHeader::LongHeader():
    IHeader(PHT_LONG_HEADER),
    version_(0),
    destination_connection_id_length_(0),
    source_connection_id_length_(0) {

}

LongHeader::LongHeader(uint8_t flag):
    IHeader(flag),
    version_(0),
    destination_connection_id_length_(0),
    source_connection_id_length_(0) {

}

LongHeader::~LongHeader() {

}

bool LongHeader::EncodeHeader(std::shared_ptr<common::IBufferWrite> buffer) {
    if (!HeaderFlag::EncodeFlag(buffer)) {
        return false;
    }

    uint16_t need_size = EncodeHeaderSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }
    
    common::BufferEncodeWrapper wrapper(buffer);

    // encode version
    wrapper.EncodeFixedUint32(version_);
    wrapper.EncodeFixedUint8(destination_connection_id_length_);
    if (destination_connection_id_length_ > 0) {
        wrapper.EncodeBytes(destination_connection_id_, destination_connection_id_length_);
    }
    wrapper.EncodeFixedUint8(source_connection_id_length_);
    if (source_connection_id_length_ > 0) {
        wrapper.EncodeBytes(source_connection_id_, source_connection_id_length_);
    }

    auto data_span = wrapper.GetDataSpan();
    // the header src include header flag
    header_src_data_ = common::BufferSpan(data_span.GetStart() - 1, data_span.GetEnd());

    return true;
}

bool LongHeader::DecodeHeader(std::shared_ptr<common::IBufferRead> buffer, bool with_flag) {
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
    // decode version
    wrapper.DecodeFixedUint32(version_);

    // decode dcid
    wrapper.DecodeFixedUint8(destination_connection_id_length_);
    if (destination_connection_id_length_ > 0) {
        auto cid = (uint8_t*)destination_connection_id_;
        wrapper.DecodeBytes(cid, destination_connection_id_length_);
    }

    // decode scid
    wrapper.DecodeFixedUint8(source_connection_id_length_);
    if (source_connection_id_length_ > 0) {
        auto cid = (uint8_t*)source_connection_id_;
        wrapper.DecodeBytes(cid, source_connection_id_length_);
    }
    
    auto data_span = wrapper.GetDataSpan();
    // the header src include header flag
    header_src_data_ = common::BufferSpan(data_span.GetStart() - 1, data_span.GetEnd());
    return true;
}

uint32_t LongHeader::EncodeHeaderSize() {
    return sizeof(LongHeader);
}

void LongHeader::SetDestinationConnectionId(uint8_t* id, uint8_t len) {
    destination_connection_id_length_ = len;
    memcpy(destination_connection_id_, id, len);
}

void LongHeader::SetSourceConnectionId(uint8_t* id, uint8_t len) {
    source_connection_id_length_ = len;
    memcpy(source_connection_id_, id, len);
}

}
}