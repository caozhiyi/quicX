#include "common/log/log.h"
#include "common/decode/decode.h"
#include "quic/frame/stop_sending_frame.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {

StopSendingFrame::StopSendingFrame(): 
    IFrame(FT_STOP_SENDING),
    _stream_id(0),
    _app_error_code(0) {

}

StopSendingFrame::~StopSendingFrame() {

}

bool StopSendingFrame::Encode(std::shared_ptr<IBufferWriteOnly> buffer) {
    uint16_t need_size = EncodeSize();
    auto pos_pair = buffer->GetWritePair();
    auto remain_size = pos_pair.second - pos_pair.first;
    if (need_size > remain_size) {
        LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    char* pos = pos_pair.first;
    pos = FixedEncodeUint16(pos, _frame_type);
    pos = EncodeVarint(pos, _stream_id);
    pos = EncodeVarint(pos, _app_error_code);

    buffer->MoveWritePt(pos - pos_pair.first);
    return true;
}

bool StopSendingFrame::Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type) {
    auto pos_pair = buffer->GetReadPair();
    char* pos = pos_pair.first;

    if (with_type) {
        pos = FixedDecodeUint16(pos, pos_pair.second, _frame_type);
        if (_frame_type != FT_STOP_SENDING) {
            return false;
        }
    }

    pos = DecodeVarint(pos, pos_pair.second, _stream_id);
    pos = DecodeVarint(pos, pos_pair.second, _app_error_code);

    buffer->MoveReadPt(pos - pos_pair.first);
    return true;
}

uint32_t StopSendingFrame::EncodeSize() {
    return sizeof(StopSendingFrame);
}

}