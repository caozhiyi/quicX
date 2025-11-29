#include "common/log/log.h"
#include "quic/frame/reset_stream_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace quic {

ResetStreamFrame::ResetStreamFrame():
    IStreamFrame(FrameType::kResetStream),
    app_error_code_(0),
    final_size_(0) {}

ResetStreamFrame::~ResetStreamFrame() {}

bool ResetStreamFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    uint16_t need_size = EncodeSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR(
            "insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    CHECK_ENCODE_ERROR(wrapper.EncodeFixedUint16(frame_type_), "failed to encode frame type");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(stream_id_), "failed to encode stream id");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(app_error_code_), "failed to encode app error code");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(final_size_), "failed to encode final size");

    return true;
}

bool ResetStreamFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        CHECK_DECODE_ERROR(wrapper.DecodeFixedUint16(frame_type_), "failed to decode frame type");
        if (frame_type_ != FrameType::kResetStream) {
            common::LOG_ERROR("invalid frame type. frame_type:%d", frame_type_);
            return false;
        }
    }
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(stream_id_), "failed to decode stream id");
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(app_error_code_), "failed to decode app error code");
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(final_size_), "failed to decode final size");
    return true;
}

uint32_t ResetStreamFrame::EncodeSize() {
    return sizeof(ResetStreamFrame);
}

}  // namespace quic
}  // namespace quicx