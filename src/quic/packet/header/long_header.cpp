#include <memory>
#include <cstring>

#include "common/log/log.h"
#include "common/decode/decode.h"
#include "quic/common/constants.h"
#include "quic/packet/header/long_header.h"
#include "common/buffer/if_buffer.h"
#include "common/alloter/if_alloter.h"

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
    
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }
    
    uint8_t* cur_pos = span.GetStart();

    // encode version
    cur_pos = common::FixedEncodeUint32(cur_pos, version_);
    
    // encode dcid
    cur_pos = common::FixedEncodeUint8(cur_pos, destination_connection_id_length_);
    if (destination_connection_id_length_ > 0) {
        memcpy(cur_pos, destination_connection_id_, destination_connection_id_length_);
        cur_pos += destination_connection_id_length_;
    }

    // encode scid
    cur_pos = common::FixedEncodeUint8(cur_pos, source_connection_id_length_);
    if (source_connection_id_length_ > 0) {
        memcpy(cur_pos, source_connection_id_, source_connection_id_length_);
        cur_pos += source_connection_id_length_;
    }
    header_src_data_ = std::move(common::BufferSpan(span.GetStart() - 1, cur_pos));

    buffer->MoveWritePt(cur_pos - span.GetStart());
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
    
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    // decode version
    pos = common::FixedDecodeUint32(pos, end, version_);

    // decode dcid
    pos = common::FixedDecodeUint8(pos, end, destination_connection_id_length_);
    memcpy(&destination_connection_id_, pos, destination_connection_id_length_);
    pos += destination_connection_id_length_;

    // decode scid
    pos = common::FixedDecodeUint8(pos, end, source_connection_id_length_);
    memcpy(&source_connection_id_, pos, source_connection_id_length_);
    pos += source_connection_id_length_;
 
    header_src_data_ = std::move(common::BufferSpan(span.GetStart() - 1, pos));
    
    buffer->MoveReadPt(pos - span.GetStart());
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