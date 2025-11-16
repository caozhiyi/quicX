#include "common/log/log.h"
#include "quic/frame/stream_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"


namespace quicx {
namespace quic {

StreamFrame::StreamFrame():
    IStreamFrame(FrameType::kStream),
    offset_(0),
    length_(0) {

}

StreamFrame::StreamFrame(uint16_t frame_type):
    IStreamFrame(frame_type),
    offset_(0) {

}

StreamFrame::~StreamFrame() {

}

bool StreamFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    uint16_t need_size = EncodeSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    wrapper.EncodeFixedUint16(frame_type_);
    wrapper.EncodeVarint(stream_id_);
    if (HasOffset()) {
        wrapper.EncodeVarint(offset_);
    }
    if (HasLength()) {
        wrapper.EncodeVarint(length_);
    }
    wrapper.EncodeBytes(data_.GetStart(), length_);
    return true;
}

bool StreamFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        wrapper.DecodeFixedUint16(frame_type_);
    }
    wrapper.DecodeVarint(stream_id_);
    if (HasOffset()) {
        wrapper.DecodeVarint(offset_);
    }
    if (HasLength()) {
        wrapper.DecodeVarint(length_);
    }
    wrapper.Flush();
    if (length_ > buffer->GetDataLength()) {
        common::LOG_ERROR("insufficient remaining data. remain_size:%d, need_size:%d", buffer->GetDataLength(), length_);
        return false;
    }
    data_ = buffer->GetSharedReadableSpan(length_);
    return true;
}

uint32_t StreamFrame::EncodeSize() {
    return sizeof(StreamFrame);
}

void StreamFrame::SetOffset(uint64_t offset) {
    offset_ = offset;
    frame_type_ |= kOffFlag;
}

bool StreamFrame::IsStreamFrame(uint16_t frame_type) {
    return (frame_type & ~kMaskFlag) == FrameType::kStream;
}

}
}