#include "common/log/log.h"
#include "quic/frame/path_response_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace quic {

PathResponseFrame::PathResponseFrame():
    IFrame(FrameType::kPathResponse) {
    memset(data_, 0, kPathDataLength);
}

PathResponseFrame::~PathResponseFrame() {}

bool PathResponseFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    uint16_t need_size = EncodeSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR(
            "insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    CHECK_ENCODE_ERROR(wrapper.EncodeFixedUint16(frame_type_), "failed to encode frame type");
    CHECK_ENCODE_ERROR(wrapper.EncodeBytes(data_, kPathDataLength), "failed to encode data");
    return true;
}

bool PathResponseFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        CHECK_DECODE_ERROR(wrapper.DecodeFixedUint16(frame_type_), "failed to decode frame type");
        if (frame_type_ != FrameType::kPathResponse) {
            common::LOG_ERROR("invalid frame type. frame_type:%d", frame_type_);
            return false;
        }
    }

    wrapper.Flush();
    if (kPathDataLength > buffer->GetDataLength()) {
        common::LOG_ERROR(
            "insufficient remaining data. remain_size:%d, need_size:%d", buffer->GetDataLength(), kPathDataLength);
        return false;
    }
    auto data = (uint8_t*)data_;
    CHECK_DECODE_ERROR(wrapper.DecodeBytes(data, kPathDataLength), "failed to decode data");
    return true;
}

uint32_t PathResponseFrame::EncodeSize() {
    return sizeof(PathResponseFrame);
}

void PathResponseFrame::SetData(uint8_t* data) {
    memcpy(data_, data, kPathDataLength);
}

}  // namespace quic
}  // namespace quicx