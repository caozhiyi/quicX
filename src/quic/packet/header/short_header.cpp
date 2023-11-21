#include <cstring>
#include "common/log/log.h"
#include "quic/packet/header/short_header.h"

namespace quicx {
namespace quic {

ShortHeader::ShortHeader():
    _destination_connection_id_length(0),
    IHeader(PHT_SHORT_HEADER) {

}

ShortHeader::ShortHeader(uint8_t flag):
    _destination_connection_id_length(0),
    IHeader(flag) {
}

ShortHeader::~ShortHeader() {

}

bool ShortHeader::EncodeHeader(std::shared_ptr<common::IBufferWrite> buffer) {
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
    if (_destination_connection_id_length > 0) {
        memcpy(cur_pos, _destination_connection_id, _destination_connection_id_length);
        cur_pos += _destination_connection_id_length;
    }
    buffer->MoveWritePt(cur_pos - span.GetStart());
    _header_src_data = std::move(common::BufferSpan(span.GetStart() - 1, cur_pos));
    return true;
}

bool ShortHeader::DecodeHeader(std::shared_ptr<common::IBufferRead> buffer, bool with_flag) {
    if (with_flag) {
        if (!HeaderFlag::DecodeFlag(buffer)) {
            return false;
        }
    }

    // check flag fixed bit
    if (!HeaderFlag::GetLongHeaderFlag()._fix_bit) {
        common::LOG_ERROR("quic fixed bit is not set");
        return false;
    }
    
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    // todo not copy
    memcpy(&_destination_connection_id, pos, _destination_connection_id_length);
    pos += _destination_connection_id_length;
 
    _header_src_data = std::move(common::BufferSpan((span.GetStart() - 1), pos));
    buffer->MoveReadPt(pos - span.GetStart());
    return true;
}

uint32_t ShortHeader::EncodeHeaderSize() {
    return sizeof(ShortHeader);
}

void ShortHeader::SetDestinationConnectionId(uint8_t* id, uint8_t len) {
    _destination_connection_id_length = len;
    if (id != nullptr) {
        memcpy(_destination_connection_id, id, len);
    }
}

}
}
