#include <cstring>
#include "common/log/log.h"
#include "common/util/random.h"
#include "common/decode/decode.h"
#include "quic/frame/path_response_frame.h"
#include "common/buffer/buffer_interface.h"
#include "quic/frame/path_challenge_frame.h"
#include "common/alloter/alloter_interface.h"

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
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = span.GetStart();
    pos = common::FixedEncodeUint16(pos, frame_type_);
    buffer->MoveWritePt(pos - span.GetStart());

    buffer->Write(data_, __path_data_length);
    return true;
}

bool PathChallengeFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = common::FixedDecodeUint16(pos, end, frame_type_);
        if (frame_type_ != FT_PATH_CHALLENGE) {
            return false;
        }
    }

    memcpy(data_, pos, __path_data_length);
    pos += __path_data_length;

    buffer->MoveReadPt(pos - span.GetStart());
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
