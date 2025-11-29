#include "common/log/log.h"
#include "quic/frame/crypto_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace quic {

CryptoFrame::CryptoFrame():
    IFrame(FrameType::kCrypto),
    offset_(0) {}

CryptoFrame::~CryptoFrame() {}

bool CryptoFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    uint16_t need_size = EncodeSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR(
            "insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    CHECK_ENCODE_ERROR(wrapper.EncodeFixedUint16(frame_type_), "failed to encode frame type");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(offset_), "failed to encode offset");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(length_), "failed to encode length");
    CHECK_ENCODE_ERROR(wrapper.EncodeBytes(data_.GetStart(), length_), "failed to encode data");
    return true;
}

bool CryptoFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        CHECK_DECODE_ERROR(wrapper.DecodeFixedUint16(frame_type_), "failed to decode frame type");
        if (frame_type_ != FrameType::kCrypto) {
            common::LOG_ERROR("invalid frame type. frame_type:%d", frame_type_);
            return false;
        }
    }
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(offset_), "failed to decode offset");
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(length_), "failed to decode length");
    if (length_ > buffer->GetDataLength()) {
        common::LOG_ERROR(
            "insufficient remaining data. remain_size:%d, need_size:%d", buffer->GetDataLength(), length_);
        return false;
    }
    wrapper.Flush();

    data_ = buffer->GetSharedReadableSpan(length_);
    buffer->MoveReadPt(length_);
    return true;
}

uint32_t CryptoFrame::EncodeSize() {
    return sizeof(CryptoFrame) + length_;
}

}  // namespace quic
}  // namespace quicx