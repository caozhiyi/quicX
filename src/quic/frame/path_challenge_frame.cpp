#include <cstring>
#include "common/log/log.h"
#include "common/util/random.h"
#include "common/decode/decode.h"
#include "quic/frame/path_response_frame.h"
#include "common/buffer/buffer_interface.h"
#include "quic/frame/path_challenge_frame.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {

std::shared_ptr<RangeRandom> PathChallengeFrame::_random = std::make_shared<RangeRandom>(0, 62);

PathChallengeFrame::PathChallengeFrame():
    IFrame(FT_PATH_CHALLENGE) {
    memset(_data, 0, __path_data_length);
}

PathChallengeFrame::~PathChallengeFrame() {

}

bool PathChallengeFrame::Encode(std::shared_ptr<IBufferWriteOnly> buffer) {
    uint16_t need_size = EncodeSize();
    auto pos_pair = buffer->GetWritePair();
    auto remain_size = pos_pair.second - pos_pair.first;
    if (need_size > remain_size) {
        LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    char* pos = pos_pair.first;
    pos = FixedEncodeUint16(pos, _frame_type);
    buffer->MoveWritePt(pos - pos_pair.first);

    buffer->Write(_data, __path_data_length);
    return true;
}

bool PathChallengeFrame::Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type) {
    auto pos_pair = buffer->GetReadPair();
    char* pos = pos_pair.first;

    if (with_type) {
        pos = FixedDecodeUint16(pos, pos_pair.second, _frame_type);
        if (_frame_type != FT_PATH_CHALLENGE) {
            return false;
        }
    }

    memcpy(_data, pos, __path_data_length);
    pos += __path_data_length;

    buffer->MoveReadPt(pos - pos_pair.first);
    return true;
}

uint32_t PathChallengeFrame::EncodeSize() {
    return sizeof(PathChallengeFrame);
}

bool PathChallengeFrame::CompareData(std::shared_ptr<PathResponseFrame> response) {
    return strncmp(_data, response->GetData(), __path_data_length) == 0;
}

void PathChallengeFrame::MakeData() {
    for (uint32_t i = 0; i < __path_data_length; i++) {
        int32_t randomChar = _random->Random();        
        if (randomChar < 26) {
            _data[i] = 'a' + randomChar;

        } else if (randomChar < 52) {
            _data[i] = 'A' + randomChar - 26;

        } else {
            _data[i] = '0' + randomChar - 52;
        }
    }
}

}
