#include <cstring>
#include "common/log/log.h"
#include "common/decode/decode.h"
#include "common/buffer/if_buffer.h"
#include "common/alloter/if_alloter.h"
#include "quic/frame/new_connection_id_frame.h"

namespace quicx {
namespace quic {

NewConnectionIDFrame::NewConnectionIDFrame():
    IFrame(FT_NEW_CONNECTION_ID),
    sequence_number_(0),
    retire_prior_to_(0) {
    memset(stateless_reset_token_, 0, __stateless_reset_token_length);
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
    pos = common::FixedEncodeUint16(pos, frame_type_);
    pos = common::EncodeVarint(pos, sequence_number_);
    pos = common::EncodeVarint(pos, retire_prior_to_);
    pos = common::FixedEncodeUint8(pos, length_);
    buffer->MoveWritePt(pos - span.GetStart());
    buffer->Write(connection_id_, length_);
    buffer->Write(stateless_reset_token_, __stateless_reset_token_length);
    return true;
}

bool NewConnectionIDFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = common::FixedDecodeUint16(pos, end, frame_type_);
    }
    pos = common::DecodeVarint(pos, end, sequence_number_);
    pos = common::DecodeVarint(pos, end, retire_prior_to_);
    pos = common::FixedDecodeUint8(pos, end, length_);
    buffer->MoveReadPt(pos - span.GetStart());
    if (buffer->Read(connection_id_, length_) != length_) {
        return false;
    }
    if (buffer->Read(stateless_reset_token_, __stateless_reset_token_length) != __stateless_reset_token_length) {
        return false;
    }
    return true;
}

uint32_t NewConnectionIDFrame::EncodeSize() {
    return sizeof(NewConnectionIDFrame) - __stateless_reset_token_length + length_;
}

void NewConnectionIDFrame::SetConnectionID(uint8_t* id, uint8_t len) {
    if (len > __max_cid_length) {
        common::LOG_ERROR("too max connecion id length. len:%d, max:%d", len, __max_cid_length);
        return;
    }
    memcpy(connection_id_, id, len);
    length_ = len;
}

void NewConnectionIDFrame::GetConnectionID(uint8_t* id, uint8_t& len) { 
    if (len < length_) {
        common::LOG_ERROR("insufficient remaining id space. remain_size:%d, need_size:%d", len, length_);
        return;
    }
    
    memcpy(id, connection_id_, length_); 
    len = length_;
}

void NewConnectionIDFrame::SetStatelessResetToken(uint8_t* token) {
    memcpy(stateless_reset_token_, token, __stateless_reset_token_length);
}

}
}