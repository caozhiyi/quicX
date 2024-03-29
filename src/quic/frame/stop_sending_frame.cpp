#include "common/log/log.h"
#include "common/decode/decode.h"
#include "quic/frame/stop_sending_frame.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {
namespace quic {

StopSendingFrame::StopSendingFrame(): 
    IStreamFrame(FT_STOP_SENDING),
    _app_error_code(0) {

}

StopSendingFrame::~StopSendingFrame() {

}

bool StopSendingFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = span.GetStart();
    pos = common::FixedEncodeUint16(pos, _frame_type);
    pos = common::EncodeVarint(pos, _stream_id);
    pos = common::EncodeVarint(pos, _app_error_code);

    buffer->MoveWritePt(pos - span.GetStart());
    return true;
}

bool StopSendingFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = common::FixedDecodeUint16(pos, end, _frame_type);
        if (_frame_type != FT_STOP_SENDING) {
            return false;
        }
    }

    pos = common::DecodeVarint(pos, end, _stream_id);
    pos = common::DecodeVarint(pos, end, _app_error_code);

    buffer->MoveReadPt(pos - span.GetStart());
    return true;
}

uint32_t StopSendingFrame::EncodeSize() {
    return sizeof(StopSendingFrame);
}

}
}