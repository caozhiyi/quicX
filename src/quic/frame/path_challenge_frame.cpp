#include <cstring>
#include "common/log/log.h"
#include "common/util/random.h"
#include "quic/frame/path_response_frame.h"
#include "quic/frame/path_challenge_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace quic {

std::shared_ptr<common::RangeRandom> PathChallengeFrame::random_ = std::make_shared<common::RangeRandom>(0, 62);

PathChallengeFrame::PathChallengeFrame():
    IFrame(FrameType::kPathChallenge) {
    memset(data_, 0, kPathDataLength);
}

PathChallengeFrame::~PathChallengeFrame() {}

bool PathChallengeFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
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

bool PathChallengeFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);
    if (with_type) {
        CHECK_DECODE_ERROR(wrapper.DecodeFixedUint16(frame_type_), "failed to decode frame type");
        if (frame_type_ != FrameType::kPathChallenge) {
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

uint32_t PathChallengeFrame::EncodeSize() {
    return sizeof(PathChallengeFrame);
}

bool PathChallengeFrame::CompareData(std::shared_ptr<PathResponseFrame> response) {
    return strncmp((const char*)data_, (const char*)response->GetData(), kPathDataLength) == 0;
}

void PathChallengeFrame::MakeData() {
    for (uint32_t i = 0; i < kPathDataLength; i++) {
        int32_t randomChar = random_->Random();
        if (randomChar < 26) {
            data_[i] = 'a' + randomChar;

        } else if (randomChar < 52) {
            data_[i] = 'A' + randomChar - 26;

        } else {
            data_[i] = '0' + randomChar - 52;
        }
    }
}

}  // namespace quic
}  // namespace quicx
