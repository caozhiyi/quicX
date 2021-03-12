#include <cstring>
#include "common/util/random.h"
#include "path_response_frame.h"
#include "path_challenge_frame.h"
#include "common/decode/normal_decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {

std::shared_ptr<RangeRandom> PathChallengeFrame::_random = std::make_shared<RangeRandom>(0, 62);

PathChallengeFrame::PathChallengeFrame():
    Frame(FT_PATH_CHALLENGE) {

}

PathChallengeFrame::~PathChallengeFrame() {

}

bool PathChallengeFrame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);

    char* pos = EncodeFixed<uint16_t>(data, _frame_type);
    buffer->Write(data, pos - data);
    buffer->Write(_data, __path_data_length);

    alloter->PoolFree(data, size);
    return true;
}

bool PathChallengeFrame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);
    uint32_t len = buffer->ReadNotMovePt(data, size);
    
    char* pos = nullptr;
    if (with_type) {
        pos = DecodeFixed<uint16_t>(data, data + size, _frame_type);
    }

    memcpy(_data, pos, __path_data_length);
    pos += __path_data_length;

    buffer->MoveReadPt(pos - data);
    alloter->PoolFree(data, size);
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
