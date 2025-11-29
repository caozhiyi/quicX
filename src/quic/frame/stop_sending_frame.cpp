#include "common/log/log.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

#include "quic/frame/stop_sending_frame.h"

namespace quicx {
namespace quic {

StopSendingFrame::StopSendingFrame():
    IStreamFrame(FrameType::kStopSending),
    app_error_code_(0) {}

StopSendingFrame::~StopSendingFrame() {}

bool StopSendingFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
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
    return true;
}

bool StopSendingFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        CHECK_DECODE_ERROR(wrapper.DecodeFixedUint16(frame_type_), "failed to decode frame type");
        if (frame_type_ != FrameType::kStopSending) {
            common::LOG_ERROR("invalid frame type. frame_type:%d", frame_type_);
            return false;
        }
    }
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(stream_id_), "failed to decode stream id");
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(app_error_code_), "failed to decode app error code");
    return true;
}

uint32_t StopSendingFrame::EncodeSize() {
    return sizeof(StopSendingFrame);
}

}  // namespace quic
}  // namespace quicx