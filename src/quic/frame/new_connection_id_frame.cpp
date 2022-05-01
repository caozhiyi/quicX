#include <cstring>
#include "common/log/log.h"
#include "common/decode/decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"
#include "quic/frame/new_connection_id_frame.h"

namespace quicx {

NewConnectionIDFrame::NewConnectionIDFrame():
    IFrame(FT_NEW_CONNECTION_ID),
    _sequence_number(0),
    _retire_prior_to(0) {
    memset(_stateless_reset_token, 0, __stateless_reset_token_length);
}

NewConnectionIDFrame::~NewConnectionIDFrame() {

}

bool NewConnectionIDFrame::Encode(std::shared_ptr<IBufferWriteOnly> buffer) {
    uint16_t need_size = EncodeSize();
    auto pos_pair = buffer->GetWritePair();
    auto remain_size = pos_pair.second - pos_pair.first;
    if (need_size > remain_size) {
        LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    char* pos = pos_pair.first;
    pos = FixedEncodeUint16(pos, _frame_type);
    pos = EncodeVarint(pos, _sequence_number);
    pos = EncodeVarint(pos, _retire_prior_to);
    pos = FixedEncodeUint8(pos, (uint8_t)_connection_id.size());
    for (size_t i = 0; i < _connection_id.size(); i++) {
        pos = EncodeVarint(pos, _connection_id[i]);
    }

    buffer->MoveWritePt(pos - pos_pair.first);

    buffer->Write(_stateless_reset_token, __stateless_reset_token_length);
    return true;
}

bool NewConnectionIDFrame::Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type) {
    auto pos_pair = buffer->GetReadPair();
    char* pos = pos_pair.first;

    if (with_type) {
        pos = FixedDecodeUint16(pos, pos_pair.second, _frame_type);
    }
    pos = DecodeVarint(pos, pos_pair.second, _sequence_number);
    pos = DecodeVarint(pos, pos_pair.second, _retire_prior_to);
    // encode normal members and number of connection id
    uint8_t connection_id_num = 0;
    pos = FixedDecodeUint8(pos, pos_pair.second, connection_id_num);

    _connection_id.resize(connection_id_num);
    for (size_t i = 0; i < connection_id_num; i++) {
        pos = DecodeVarint(pos, pos_pair.second, _connection_id[i]);
    }
    
    buffer->MoveReadPt(pos - pos_pair.first);
    if (buffer->Read(_stateless_reset_token, __stateless_reset_token_length) != __stateless_reset_token_length) {
        return false;
    }
    return true;
}

uint32_t NewConnectionIDFrame::EncodeSize() {
    return sizeof(NewConnectionIDFrame) - __stateless_reset_token_length + _connection_id.size() * sizeof(uint64_t);
}

void NewConnectionIDFrame::SetStatelessResetToken(char* token) {
    memcpy(_stateless_reset_token, token, __stateless_reset_token_length);
}

}