
#include <memory>

#include "long_header.h"
#include "common/decode/normal_decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {

LongHeaderPacket::LongHeaderPacket():
    _version(0),
    _destination_connection_id_length(0),
    _source_connection_id_length(0) {
    _header_format._header = 0;
    memset(_destination_connection_id, 0, __connection_length_max);
    memset(_source_connection_id, 0, __connection_length_max);
}

LongHeaderPacket::~LongHeaderPacket() {

}

bool LongHeaderPacket::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    int size = EncodeSize();
    char* data = alloter->PoolMalloc<char>(size);
    char* pos = data;

    pos = EncodeFixed<uint8_t>(pos, _header_format._header);
    pos = EncodeVarint(pos, _version);
   
    pos = EncodeFixed<uint8_t>(pos, _destination_connection_id_length);
    buffer->Write(data, pos - data);
    buffer->Write(_destination_connection_id, _destination_connection_id_length);

    pos = data;
    pos = EncodeFixed<uint8_t>(pos, _source_connection_id_length);
    buffer->Write(data, pos - data);
    buffer->Write(_source_connection_id, _source_connection_id_length);

    alloter->PoolFree(data, size);

    return true;
}

bool LongHeaderPacket::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();
    char* data = alloter->PoolMalloc<char>(size);
    size = buffer->ReadNotMovePt(data, size);
    char* pos = data;
    char* end = data + size;

    pos = DecodeFixed<uint8_t>(pos, end, _header_format._header);
    pos = DecodeVarint(pos, end, _version);
   
    pos = DecodeFixed<uint8_t>(pos, end, _destination_connection_id_length);
    memcpy(&_destination_connection_id, pos, _destination_connection_id_length);
    pos += _destination_connection_id_length;

    pos = DecodeFixed<uint8_t>(pos, end, _source_connection_id_length);
    memcpy(&_source_connection_id, pos, _source_connection_id_length);
    pos += _source_connection_id_length;
 
    buffer->MoveReadPt(pos - data);
    alloter->PoolFree(data, size);

    return true;
}

uint32_t LongHeaderPacket::EncodeSize() {
    return sizeof(LongHeaderPacket);
}

}