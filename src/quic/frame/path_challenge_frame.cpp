#include "quic/frame/path_challenge_frame.h"
#include <cstring>
#include <openssl/mem.h>
#include <openssl/rand.h>
#include "common/buffer/buffer_decode_wrapper.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/log/log.h"
#include "quic/frame/path_response_frame.h"

namespace quicx {
namespace quic {

PathChallengeFrame::PathChallengeFrame():
    IFrame(FrameType::kPathChallenge) {
    memset(data_, 0, kPathDataLength);
}

PathChallengeFrame::~PathChallengeFrame() {}

bool PathChallengeFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    uint32_t need_size = EncodeSize();
    if (need_size > buffer->GetFreeLength()) {
        LOG_ERROR(
            "insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(frame_type_), "failed to encode frame type");
    CHECK_ENCODE_ERROR(wrapper.EncodeBytes(data_, kPathDataLength), "failed to encode data");
    return true;
}

bool PathChallengeFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);
    if (with_type) {
        uint64_t type = 0;
        CHECK_DECODE_ERROR(wrapper.DecodeVarint(type), "failed to decode frame type");
        frame_type_ = static_cast<uint16_t>(type);
        if (frame_type_ != FrameType::kPathChallenge) {
            LOG_ERROR("invalid frame type. frame_type:%d", frame_type_);
            return false;
        }
    }
    wrapper.Flush();
    if (kPathDataLength > buffer->GetDataLength()) {
        LOG_ERROR(
            "insufficient remaining data. remain_size:%d, need_size:%d", buffer->GetDataLength(), kPathDataLength);
        return false;
    }
    auto data = (uint8_t*)data_;
    CHECK_DECODE_ERROR(wrapper.DecodeBytes(data, kPathDataLength), "failed to decode data");
    return true;
}

uint32_t PathChallengeFrame::EncodeSize() {
    return common::GetEncodeVarintLength(frame_type_) + kPathDataLength;
}

bool PathChallengeFrame::CompareData(std::shared_ptr<PathResponseFrame> response) {
    // The token is opaque binary, so it may contain embedded NUL bytes: a string
    // compare would stop early and accept a forged prefix. CRYPTO_memcmp also keeps
    // the comparison constant time so the token cannot be recovered byte by byte.
    return CRYPTO_memcmp(data_, response->GetData(), kPathDataLength) == 0;
}

bool PathChallengeFrame::MakeData() {
    if (RAND_bytes(data_, kPathDataLength) != 1) {
        memset(data_, 0, kPathDataLength);
        LOG_ERROR("failed to generate path challenge data from the cryptographic RNG");
        return false;
    }
    return true;
}

}  // namespace quic
}  // namespace quicx
