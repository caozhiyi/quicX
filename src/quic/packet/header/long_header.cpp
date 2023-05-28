#include <memory>
#include <cstring>

#include "common/log/log.h"
#include "common/decode/decode.h"
#include "quic/common/constants.h"
#include "quic/packet/header/long_header.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {

LongHeader::LongHeader():
    IHeader(PHT_LONG_HEADER),
    _version(0),
    _destination_connection_id_length(0),
    _source_connection_id_length(0) {

}

LongHeader::LongHeader(uint8_t flag):
    IHeader(flag),
    _version(0),
    _destination_connection_id_length(0),
    _source_connection_id_length(0) {

}

LongHeader::~LongHeader() {

}

bool LongHeader::EncodeHeader(std::shared_ptr<IBufferWrite> buffer) {
    if (!HeaderFlag::EncodeFlag(buffer)) {
        return false;
    }

    uint16_t need_size = EncodeHeaderSize();
    
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }
    
    uint8_t* cur_pos = span.GetStart();
    cur_pos = FixedEncodeUint32(cur_pos, _version);
    cur_pos = FixedEncodeUint8(cur_pos, _destination_connection_id_length);
    if (_destination_connection_id > 0) {
        memcpy(cur_pos, _destination_connection_id, _destination_connection_id_length);
        cur_pos += _destination_connection_id_length;
    }

    cur_pos = FixedEncodeUint8(cur_pos, _source_connection_id_length);
    if (_source_connection_id_length > 0) {
        memcpy(cur_pos, _source_connection_id, _source_connection_id_length);
        cur_pos += _source_connection_id_length;
    }
    buffer->MoveWritePt(cur_pos - span.GetStart());
    _header_src_data = std::move(BufferSpan(span.GetStart() - 1, cur_pos));
    return true;
}

bool LongHeader::DecodeHeader(std::shared_ptr<IBufferRead> buffer, bool with_flag) {
    if (with_flag) {
        if (!HeaderFlag::DecodeFlag(buffer)) {
            return false;
        }
    }

    // check flag fixed bit
    if (!HeaderFlag::GetLongHeaderFlag()._fix_bit) {
        LOG_ERROR("quic fixed bit is not set");
        return false;
    }
    
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    pos = FixedDecodeUint32(pos, end, _version);
    pos = FixedDecodeUint8(pos, end, _destination_connection_id_length);
    // todo not copy
    memcpy(&_destination_connection_id, pos, _destination_connection_id_length);
    pos += _destination_connection_id_length;

    pos = FixedDecodeUint8(pos, end, _source_connection_id_length);
    // todo not copy
    memcpy(&_source_connection_id, pos, _source_connection_id_length);
    pos += _source_connection_id_length;
 
    _header_src_data = std::move(BufferSpan(span.GetStart() - (with_flag ? 1 : 0), pos));
    buffer->MoveReadPt(pos - span.GetStart());
    return true;
}

uint32_t LongHeader::EncodeHeaderSize() {
    return sizeof(LongHeader);
}

void LongHeader::SetDestinationConnectionId(uint8_t* id, uint8_t len) {
    _destination_connection_id_length = len;
    memcpy(_destination_connection_id, id, len);
}

void LongHeader::SetSourceConnectionId(uint8_t* id, uint8_t len) {
    _source_connection_id_length = len;
    memcpy(_source_connection_id, id, len);
}

}