#include <cstring>
#include "common/log/log.h"
#include "quic/frame/new_connection_id_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

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
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    wrapper.EncodeFixedUint16(frame_type_);
    wrapper.EncodeVarint(sequence_number_);
    wrapper.EncodeVarint(retire_prior_to_);
    wrapper.EncodeFixedUint8(length_);
    wrapper.EncodeBytes(connection_id_, length_);
    wrapper.EncodeBytes(stateless_reset_token_, __stateless_reset_token_length);

    return true;
}

bool NewConnectionIDFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        wrapper.DecodeFixedUint16(frame_type_);
        if (frame_type_ != FT_NEW_CONNECTION_ID) {
            return false;
        }
    }
    wrapper.DecodeVarint(sequence_number_);
    wrapper.DecodeVarint(retire_prior_to_);
    wrapper.DecodeFixedUint8(length_);

    wrapper.Flush();
    if (length_ > buffer->GetDataLength()) {
        common::LOG_ERROR("insufficient remaining data. remain_size:%d, need_size:%d", buffer->GetDataLength(), length_);
        return false;
    }
    auto data = (uint8_t*)connection_id_;
    wrapper.DecodeBytes(data, length_);
    
    wrapper.Flush();
    if (__stateless_reset_token_length > buffer->GetDataLength()) {
        common::LOG_ERROR("insufficient remaining data. remain_size:%d, need_size:%d", buffer->GetDataLength(), __stateless_reset_token_length);
        return false;
    }
    data = (uint8_t*)stateless_reset_token_;
    wrapper.DecodeBytes(data, __stateless_reset_token_length);
    
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