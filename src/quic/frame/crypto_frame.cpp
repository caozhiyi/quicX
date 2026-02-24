#include "quic/frame/crypto_frame.h"
#include "common/buffer/buffer_decode_wrapper.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/log/log.h"

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
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(frame_type_), "failed to encode frame type");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(offset_), "failed to encode offset");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(length_), "failed to encode length");
    CHECK_ENCODE_ERROR(wrapper.EncodeBytes(data_.GetStart(), length_), "failed to encode data");
    return true;
}

bool CryptoFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        uint64_t type = 0;
        CHECK_DECODE_ERROR(wrapper.DecodeVarint(type), "failed to decode frame type");
        frame_type_ = (uint16_t)type;

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

static uint32_t GetVarintSize(uint64_t value) {
    if (value < 64) {
        return 1;
    }
    if (value < 16384) {
        return 2;
    }
    if (value < 1073741824) {
        return 4;
    }
    return 8;
}

uint32_t CryptoFrame::EncodeSize() {
    return GetVarintSize(frame_type_) + GetVarintSize(offset_) + GetVarintSize(length_) + length_;
}

}  // namespace quic
}  // namespace quicx