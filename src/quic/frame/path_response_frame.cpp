#include "common/log/log.h"
#include "common/decode/decode.h"
#include "common/buffer/buffer_interface.h"
#include "quic/frame/path_response_frame.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {


PathResponseFrame::PathResponseFrame(): 
    IFrame(FT_PATH_RESPONSE) {
    memset(_data, 0, __path_data_length);
}

PathResponseFrame::~PathResponseFrame() {

}

bool PathResponseFrame::Encode(std::shared_ptr<IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    auto pos_pair = buffer->GetWritePair();
    auto remain_size = pos_pair.second - pos_pair.first;
    if (need_size > remain_size) {
        LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = pos_pair.first;
    pos = FixedEncodeUint16(pos, _frame_type);
    buffer->MoveWritePt(pos - pos_pair.first);

    buffer->Write(_data, __path_data_length);
    return true;
}

bool PathResponseFrame::Decode(std::shared_ptr<IBufferRead> buffer, bool with_type) {
    auto pos_pair = buffer->GetReadPair();
    uint8_t* pos = pos_pair.first;

    if (with_type) {
        pos = FixedDecodeUint16(pos, pos_pair.second, _frame_type);
        if (_frame_type != FT_PATH_RESPONSE) {
            return false;
        } 
    }

    memcpy(_data, pos, __path_data_length);
    pos += __path_data_length;

    buffer->MoveReadPt(pos - pos_pair.first);
    return true;
}

uint32_t PathResponseFrame::EncodeSize() {
    return sizeof(PathResponseFrame);
}

void PathResponseFrame::SetData(uint8_t* data) {
    memcpy(_data, data, __path_data_length);
}

}