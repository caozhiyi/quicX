#include "common/log/log.h"
#include "common/decode/decode.h"
#include "common/buffer/buffer_interface.h"
#include "quic/frame/path_response_frame.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {
namespace quic {


PathResponseFrame::PathResponseFrame(): 
    IFrame(FT_PATH_RESPONSE) {
    memset(_data, 0, __path_data_length);
}

PathResponseFrame::~PathResponseFrame() {

}

bool PathResponseFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = span.GetStart();
    pos = common::FixedEncodeUint16(pos, _frame_type);
    buffer->MoveWritePt(pos - span.GetStart());

    buffer->Write(_data, __path_data_length);
    return true;
}

bool PathResponseFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = common::FixedDecodeUint16(pos, end, _frame_type);
        if (_frame_type != FT_PATH_RESPONSE) {
            return false;
        } 
    }

    memcpy(_data, pos, __path_data_length);
    pos += __path_data_length;

    buffer->MoveReadPt(pos - span.GetStart());
    return true;
}

uint32_t PathResponseFrame::EncodeSize() {
    return sizeof(PathResponseFrame);
}

void PathResponseFrame::SetData(uint8_t* data) {
    memcpy(_data, data, __path_data_length);
}

}
}