#include <cstring>
#include "common/log/log.h"
#include "common/decode/decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"
#include "quic/frame/new_connection_id_frame.h"

namespace quicx {
namespace quic {

NewConnectionIDFrame::NewConnectionIDFrame():
    IFrame(FT_NEW_CONNECTION_ID),
    _sequence_number(0),
    _retire_prior_to(0) {
    memset(_stateless_reset_token, 0, __stateless_reset_token_length);
}

NewConnectionIDFrame::~NewConnectionIDFrame() {

}

bool NewConnectionIDFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = span.GetStart();
    pos = common::FixedEncodeUint16(pos, _frame_type);
    pos = common::EncodeVarint(pos, _sequence_number);
    pos = common::EncodeVarint(pos, _retire_prior_to);
    pos = common::FixedEncodeUint8(pos, _length);
    buffer->MoveWritePt(pos - span.GetStart());
    buffer->Write(_connection_id, _length);
    buffer->Write(_stateless_reset_token, __stateless_reset_token_length);
    return true;
}

bool NewConnectionIDFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = common::FixedDecodeUint16(pos, end, _frame_type);
    }
    pos = common::DecodeVarint(pos, end, _sequence_number);
    pos = common::DecodeVarint(pos, end, _retire_prior_to);
    pos = common::FixedDecodeUint8(pos, end, _length);
    buffer->MoveReadPt(pos - span.GetStart());
    if (buffer->Read(_connection_id, _length) != _length) {
        return false;
    }
    if (buffer->Read(_stateless_reset_token, __stateless_reset_token_length) != __stateless_reset_token_length) {
        return false;
    }
    return true;
}

uint32_t NewConnectionIDFrame::EncodeSize() {
    return sizeof(NewConnectionIDFrame) - __stateless_reset_token_length + _length;
}

void NewConnectionIDFrame::SetConnectionID(uint8_t* id, uint8_t len) {
    if (len > __max_cid_length) {
        common::LOG_ERROR("too max connecion id length. len:%d, max:%d", len, __max_cid_length);
        return;
    }
    memcpy(_connection_id, id, len);
    _length = len;
}

void NewConnectionIDFrame::GetConnectionID(uint8_t* id, uint8_t& len) { 
    if (len < _length) {
        common::LOG_ERROR("insufficient remaining id space. remain_size:%d, need_size:%d", len, _length);
        return;
    }
    
    memcpy(id, _connection_id, _length); 
    len = _length;
}

void NewConnectionIDFrame::SetStatelessResetToken(uint8_t* token) {
    memcpy(_stateless_reset_token, token, __stateless_reset_token_length);
}

}
}