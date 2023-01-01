#include <memory>
#include <cstring>

#include "common/log/log.h"
#include "common/decode/decode.h"
#include "quic/common/constants.h"
#include "quic/packet/long_header.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {

LongHeader::LongHeader():
    _version(0),
    _destination_connection_id_length(0),
    _source_connection_id_length(0) {
    memset(_destination_connection_id, 0, __max_connection_length);
    memset(_source_connection_id, 0, __max_connection_length);
}

LongHeader::LongHeader(HeaderFlag flag):
    IHeader(flag),
    _version(0),
    _destination_connection_id_length(0),
    _source_connection_id_length(0) {
    memset(_destination_connection_id, 0, __max_connection_length);
    memset(_source_connection_id, 0, __max_connection_length);
}

LongHeader::~LongHeader() {

}

bool LongHeader::Encode(std::shared_ptr<IBufferWrite> buffer) {
    if (!_flag.Encode(buffer)) {
        return false;
    }

    uint16_t need_size = EncodeSize();
    
    auto pos_pair = buffer->GetWritePair();
    auto remain_size = pos_pair.second - pos_pair.first;
    if (need_size > remain_size) {
        LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }
    
    uint8_t* pos = pos_pair.first;
    pos = FixedEncodeUint32(pos, _version);
    pos = FixedEncodeUint8(pos, _destination_connection_id_length);

    buffer->MoveWritePt(pos - pos_pair.first);
    buffer->Write(_destination_connection_id, _destination_connection_id_length);
    pos += _destination_connection_id_length;

    pos = FixedEncodeUint8(pos, _source_connection_id_length);
    buffer->MoveWritePt(pos - pos_pair.first);

    buffer->Write(_source_connection_id, _source_connection_id_length);
    return true;
}

bool LongHeader::Decode(std::shared_ptr<IBufferRead> buffer, bool with_flag) {
    if (with_flag) {
        if (_flag.Decode(buffer)) {
            return false;
        }
    }

    // check flag fixed bit
    if (!_flag.GetLongHeaderFlag()._fix_bit) {
        LOG_ERROR("quic fixed bit is not set");
        return false;
    }
    
    auto pos_pair = buffer->GetReadPair();
    uint8_t* pos = pos_pair.first;
    pos = FixedDecodeUint32(pos, pos_pair.second, _version);
    pos = FixedDecodeUint8(pos, pos_pair.second, _destination_connection_id_length);
    // todo not copy
    memcpy(&_destination_connection_id, pos, _destination_connection_id_length);
    pos += _destination_connection_id_length;

    pos = FixedDecodeUint8(pos, pos_pair.second, _source_connection_id_length);
    // todo not copy
    memcpy(&_source_connection_id, pos, _source_connection_id_length);
    pos += _source_connection_id_length;
 
    buffer->MoveReadPt(pos - pos_pair.first);

    LOG_DEBUG("get destination connect id:%s", _destination_connection_id);
    LOG_DEBUG("get source connect id:%s", _source_connection_id);

    return true;
}

uint32_t LongHeader::EncodeSize() {
    return sizeof(LongHeader);
}

PacketType LongHeader::GetPacketType() const {
    if (_version == 0) {
        return PT_NEGOTIATION;
    }
    return PacketType(_flag.GetLongHeaderFlag()._packet_type);
}

uint32_t LongHeader::GetVersion() const {
    return _version;
}

}