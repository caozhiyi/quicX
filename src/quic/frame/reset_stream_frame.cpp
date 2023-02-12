#include "common/log/log.h"
#include "common/decode/decode.h"
#include "quic/frame/reset_stream_frame.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {

ResetStreamFrame::ResetStreamFrame(): 
    IFrame(FT_RESET_STREAM),
    _stream_id(0),
    _app_error_code(0), 
    _final_size(0) {

}

ResetStreamFrame::~ResetStreamFrame() {

}

bool ResetStreamFrame::Encode(std::shared_ptr<IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = span.GetStart();
    pos = FixedEncodeUint16(pos, _frame_type);
    pos = EncodeVarint(pos, _stream_id);
    pos = EncodeVarint(pos, _app_error_code);
    pos = EncodeVarint(pos, _final_size);

    buffer->MoveWritePt(pos - span.GetStart());
    return true;
}

bool ResetStreamFrame::Decode(std::shared_ptr<IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = FixedDecodeUint16(pos, end, _frame_type);
        if (_frame_type != FT_RESET_STREAM) {
            return false;
        }
    }
    pos = DecodeVarint(pos, end, _stream_id);
    pos = DecodeVarint(pos, end, _app_error_code);
    pos = DecodeVarint(pos, end, _final_size);

    buffer->MoveReadPt(pos - span.GetStart());
    return true;
}

uint32_t ResetStreamFrame::EncodeSize() {
    return sizeof(ResetStreamFrame);
}

}