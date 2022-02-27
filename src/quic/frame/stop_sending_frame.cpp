#include "stop_sending_frame.h"
#include "common/decode/decode.h"
#include "common/decode/normal_decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {

StopSendingFrame::StopSendingFrame(): 
    Frame(FT_STOP_SENDING),
    _stream_id(0),
    _app_error_code(0) {

}

StopSendingFrame::~StopSendingFrame() {

}

bool StopSendingFrame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();
    char* data = alloter->PoolMalloc<char>(size);
    char* pos = data;

    pos = EncodeFixed<uint16_t>(pos, _frame_type);
    pos = EncodeVarint(pos, _stream_id);
    pos = EncodeVarint(pos, _app_error_code);

    buffer->Write(data, pos - data);
    alloter->PoolFree(data, size);
    return true;
}

bool StopSendingFrame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    uint16_t size = EncodeSize();
    char* data = alloter->PoolMalloc<char>(size);
    buffer->ReadNotMovePt(data, size);
    char* pos = data;

    if (with_type) {
        pos = DecodeFixed<uint16_t>(data, data + size, _frame_type);
        if (_frame_type != FT_STOP_SENDING) {
            return false;
        }
    }

    pos = DecodeVarint(pos, data + size, _stream_id);
    pos = DecodeVarint(pos, data + size, _app_error_code);

    buffer->MoveReadPt(pos - data);
    alloter->PoolFree(data, size);
    return true;
}

uint32_t StopSendingFrame::EncodeSize() {
    return sizeof(StopSendingFrame);
}

}