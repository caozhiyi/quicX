#include "common/log/log.h"
#include "common/decode/decode.h"
#include "quic/frame/max_streams_frame.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {
namespace quic {

MaxStreamsFrame::MaxStreamsFrame(uint16_t frame_type): 
    IFrame(frame_type),
    _maximum_streams(0) {

}

MaxStreamsFrame::~MaxStreamsFrame() {

}

bool MaxStreamsFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = span.GetStart();
    pos = common::FixedEncodeUint16(pos, _frame_type);
    pos = common::EncodeVarint(pos, _maximum_streams);

    buffer->MoveWritePt(pos - span.GetStart());
    return true;
}

bool MaxStreamsFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = common::FixedDecodeUint16(pos, end, _frame_type);
        if (_frame_type != FT_MAX_STREAMS_BIDIRECTIONAL && _frame_type != FT_MAX_STREAMS_UNIDIRECTIONAL) {
            return false;
        }
    }
    pos = common::DecodeVarint(pos, end, _maximum_streams);

    buffer->MoveReadPt(pos - span.GetStart());
    return true;
}

uint32_t MaxStreamsFrame::EncodeSize() {
    return sizeof(MaxStreamsFrame);
}

}
}