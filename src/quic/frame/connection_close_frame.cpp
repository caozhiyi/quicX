#include "common/log/log.h"
#include "quic/frame/connection_close_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace quic {

ConnectionCloseFrame::ConnectionCloseFrame():
    IFrame(FrameType::kConnectionClose),
    is_application_error_(false),
    error_code_(0),
    err_frame_type_(0) {}

ConnectionCloseFrame::ConnectionCloseFrame(uint16_t frame_type):
    IFrame(frame_type),
    is_application_error_(false),
    error_code_(0),
    err_frame_type_(0) {}

ConnectionCloseFrame::~ConnectionCloseFrame() {}

bool ConnectionCloseFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    uint16_t need_size = EncodeSize();

    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR(
            "insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    CHECK_ENCODE_ERROR(wrapper.EncodeFixedUint16(frame_type_), "failed to encode frame type");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(error_code_), "failed to encode error code");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(err_frame_type_), "failed to encode err frame type");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(reason_.length()), "failed to encode reason length");

    CHECK_ENCODE_ERROR(wrapper.EncodeBytes((uint8_t*)reason_.data(), reason_.length()), "failed to encode reason");
    return true;
}

bool ConnectionCloseFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    uint16_t size = EncodeSize();

    common::BufferDecodeWrapper wrapper(buffer);
    if (with_type) {
        CHECK_DECODE_ERROR(wrapper.DecodeFixedUint16(frame_type_), "failed to decode frame type");
        if (frame_type_ != FrameType::kConnectionClose) {
            common::LOG_ERROR("invalid frame type. frame_type:%d", frame_type_);
            return false;
        }
    }

    uint32_t reason_length = 0;
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(error_code_), "failed to decode error code");
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(err_frame_type_), "failed to decode err frame type");
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(reason_length), "failed to decode reason length");
    wrapper.Flush();

    if (reason_length > buffer->GetDataLength()) {
        return false;
    }

    reason_.resize(reason_length);
    auto data = (uint8_t*)reason_.data();
    CHECK_DECODE_ERROR(wrapper.DecodeBytes(data, reason_length), "failed to decode reason");
    return true;
}

uint32_t ConnectionCloseFrame::EncodeSize() {
    return sizeof(ConnectionCloseFrame);
}

}  // namespace quic
}  // namespace quicx