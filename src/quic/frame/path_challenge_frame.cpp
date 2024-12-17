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
    IFrame(FT_PATH_CHALLENGE) {
    memset(data_, 0, __path_data_length);
}

PathChallengeFrame::~PathChallengeFrame() {

}

bool PathChallengeFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    wrapper.EncodeFixedUint16(frame_type_);
    wrapper.EncodeBytes(data_, __path_data_length);
    return true;
}

bool PathChallengeFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);
    if (with_type) {
        wrapper.DecodeFixedUint16(frame_type_);
        if (frame_type_ != FT_PATH_CHALLENGE) {
            return false;
        }
    }
    wrapper.Flush();
    if (__path_data_length > buffer->GetDataLength()) {
        common::LOG_ERROR("insufficient remaining data. remain_size:%d, need_size:%d", buffer->GetDataLength(), __path_data_length);
        return false;
    }
    auto data = (uint8_t*)data_;
    wrapper.DecodeBytes(data, __path_data_length);
    return true;
}

uint32_t PathChallengeFrame::EncodeSize() {
    return sizeof(PathChallengeFrame);
}

bool PathChallengeFrame::CompareData(std::shared_ptr<PathResponseFrame> response) {
    return strncmp((const char*)data_, (const char*)response->GetData(), __path_data_length) == 0;
}

void PathChallengeFrame::MakeData() {
    for (uint32_t i = 0; i < __path_data_length; i++) {
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

}
}
