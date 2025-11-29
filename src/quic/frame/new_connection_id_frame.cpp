#include <cstring>
#include "common/log/log.h"
#include "common/decode/decode.h"
#include "quic/frame/new_connection_id_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace quic {

NewConnectionIDFrame::NewConnectionIDFrame():
    IFrame(FrameType::kNewConnectionId),
    sequence_number_(0),
    retire_prior_to_(0),
    length_(0) {
    memset(connection_id_, 0, kMaxCidLength);
    memset(stateless_reset_token_, 0, kStatelessResetTokenLength);
}

NewConnectionIDFrame::~NewConnectionIDFrame() {}

bool NewConnectionIDFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    uint16_t need_size = EncodeSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR(
            "insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    CHECK_ENCODE_ERROR(wrapper.EncodeFixedUint16(frame_type_), "failed to encode frame type");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(sequence_number_), "failed to encode sequence number");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(retire_prior_to_), "failed to encode retire prior to");
    CHECK_ENCODE_ERROR(wrapper.EncodeFixedUint8(length_), "failed to encode length");
    CHECK_ENCODE_ERROR(wrapper.EncodeBytes(connection_id_, length_), "failed to encode connection id");
    CHECK_ENCODE_ERROR(wrapper.EncodeBytes(stateless_reset_token_, kStatelessResetTokenLength), "failed to encode stateless reset token");

    return true;
}

bool NewConnectionIDFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        CHECK_DECODE_ERROR(wrapper.DecodeFixedUint16(frame_type_), "failed to decode frame type");
        if (frame_type_ != FrameType::kNewConnectionId) {
            common::LOG_ERROR("invalid frame type. frame_type:%d", frame_type_);
            return false;
        }
    }
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(sequence_number_), "failed to decode sequence number");
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(retire_prior_to_), "failed to decode retire prior to");
    CHECK_DECODE_ERROR(wrapper.DecodeFixedUint8(length_), "failed to decode length");

    // Validate connection ID length
    if (length_ > kMaxCidLength) {
        common::LOG_ERROR("connection ID length too large. length:%d, max:%d", length_, kMaxCidLength);
        return false;
    }

    // Check if we have enough data for connection ID
    if (length_ > buffer->GetDataLength()) {
        common::LOG_ERROR(
            "insufficient data for connection ID. need:%d, available:%d", length_, buffer->GetDataLength());
        return false;
    }

    // Clear connection ID buffer before reading
    memset(connection_id_, 0, kMaxCidLength);
    uint8_t* conn_id_ptr = connection_id_;
    CHECK_DECODE_ERROR(wrapper.DecodeBytes(conn_id_ptr, length_), "failed to decode connection id");

    // Check if we have enough data for stateless reset token
    if (kStatelessResetTokenLength > buffer->GetDataLength()) {
        common::LOG_ERROR("insufficient data for stateless reset token. need:%d, available:%d",
            kStatelessResetTokenLength, buffer->GetDataLength());
        return false;
    }

    uint8_t* token_ptr = stateless_reset_token_;
    CHECK_DECODE_ERROR(wrapper.DecodeBytes(token_ptr, kStatelessResetTokenLength), "failed to decode stateless reset token");

    return true;
}

uint32_t NewConnectionIDFrame::EncodeSize() {
    // Calculate the actual size needed for varint encoding
    uint32_t sequence_size = common::GetEncodeVarintLength(sequence_number_);
    uint32_t retire_size = common::GetEncodeVarintLength(retire_prior_to_);

    // Fixed sizes: frame type (2) + length (1) + connection ID (length_) + token (16)
    uint32_t fixed_size = 2 + 1 + length_ + kStatelessResetTokenLength;

    return sequence_size + retire_size + fixed_size;
}

void NewConnectionIDFrame::SetConnectionID(uint8_t* id, uint8_t len) {
    if (len > kMaxCidLength) {
        common::LOG_ERROR("too max connection id length. len:%d, max:%d", len, kMaxCidLength);
        return;
    }
    memcpy(connection_id_, id, len);
    length_ = len;
}

void NewConnectionIDFrame::GetConnectionID(ConnectionID& id) {
    id.SetID(connection_id_, length_);
    id.SetSequenceNumber(sequence_number_);
}

void NewConnectionIDFrame::SetStatelessResetToken(uint8_t* token) {
    memcpy(stateless_reset_token_, token, kStatelessResetTokenLength);
}

}  // namespace quic
}  // namespace quicx