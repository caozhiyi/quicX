#include <cstring>
#include "new_connection_id_frame.h"
#include "common/decode/normal_decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {

NewConnectionIDFrame::NewConnectionIDFrame():
    Frame(FT_NEW_CONNECTION_ID),
    _sequence_number(0),
    _retire_prior_to(0) {
    memset(_stateless_reset_token, 0, __stateless_reset_token_length);
}

NewConnectionIDFrame::~NewConnectionIDFrame() {

}

bool NewConnectionIDFrame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);

    char* pos = EncodeFixed<uint16_t>(data, _frame_type);
    pos = EncodeVarint(pos, _sequence_number);
    pos = EncodeVarint(pos, _retire_prior_to);
    pos = EncodeFixed<uint8_t>(pos, (uint8_t)_connection_id.size());
    for (size_t i = 0; i < _connection_id.size(); i++) {
        pos = EncodeVarint(pos, _connection_id[i]);
    }
    buffer->Write(data, pos - data);
    alloter->PoolFree(data, size);

    buffer->Write(_stateless_reset_token, __stateless_reset_token_length);
    
    return true;
}

bool NewConnectionIDFrame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);
    buffer->ReadNotMovePt(data, size);

    // encode normal members and number of connection id
    uint8_t connection_id_num = 0;
    char* pos = data;
    if (with_type) {
        uint16_t type = 0;
        pos = DecodeFixed<uint16_t>(data, data + size, type);
        _frame_type = (FrameType)type;
    }
    pos = DecodeVirint(pos, data + size, _sequence_number);
    pos = DecodeVirint(pos, data + size, _retire_prior_to);
    pos = DecodeFixed<uint8_t>(pos, data + size, connection_id_num);

    buffer->MoveReadPt(pos - data);
    alloter->PoolFree(data, size);

    // encode connection ids
    size = connection_id_num * sizeof(uint64_t);
    data = alloter->PoolMalloc<char>(size);

    _connection_id.resize(connection_id_num);
    buffer->ReadNotMovePt(data, size);
    pos = data;

    for (size_t i = 0; i < connection_id_num; i++) {
        pos = DecodeVirint(pos, data + size, _connection_id[i]);
    }
    
    buffer->MoveReadPt(pos - data);
    alloter->PoolFree(data, size);
    

    buffer->Read(_stateless_reset_token, __stateless_reset_token_length);

    return true;
}

uint32_t NewConnectionIDFrame::EncodeSize() {
    return sizeof(NewConnectionIDFrame) - __stateless_reset_token_length + _connection_id.size() * sizeof(uint64_t);
}

void NewConnectionIDFrame::SetStatelessResetToken(char* token) {
    memcpy(_stateless_reset_token, token, __stateless_reset_token_length);
}

}