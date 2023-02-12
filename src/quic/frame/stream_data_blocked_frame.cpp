#include "common/log/log.h"
#include "common/decode/decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"
#include "quic/frame/stream_data_blocked_frame.h"

namespace quicx {

StreamDataBlockedFrame::StreamDataBlockedFrame():
    IFrame(FT_STREAM_DATA_BLOCKED),
    _stream_id(0),
    _maximum_data(0) {

}

StreamDataBlockedFrame::~StreamDataBlockedFrame() {

}

bool StreamDataBlockedFrame::Encode(std::shared_ptr<IBufferWrite> buffer) {
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
    pos = EncodeVarint(pos, _maximum_data);

    buffer->MoveWritePt(pos - span.GetStart());
    return true;
}

bool StreamDataBlockedFrame::Decode(std::shared_ptr<IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = FixedDecodeUint16(pos, end, _frame_type);
        if (_frame_type != FT_STREAM_DATA_BLOCKED) {
            return false;
        }
    }
    pos = DecodeVarint(pos, end, _stream_id);
    pos = DecodeVarint(pos, end, _maximum_data);

    buffer->MoveReadPt(pos - span.GetStart());
    return true;
}

uint32_t StreamDataBlockedFrame::EncodeSize() {
    return sizeof(StreamDataBlockedFrame);
}

}