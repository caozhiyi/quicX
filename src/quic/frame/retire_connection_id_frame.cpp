#include "common/log/log.h"
#include "common/decode/decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"
#include "quic/frame/retire_connection_id_frame.h"

namespace quicx {
namespace quic {

RetireConnectionIDFrame::RetireConnectionIDFrame():
    IFrame(FT_RETIRE_CONNECTION_ID),
    _sequence_number(0) {

}

RetireConnectionIDFrame::~RetireConnectionIDFrame() {

}

bool RetireConnectionIDFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = span.GetStart();
    pos = common::FixedEncodeUint16(pos, _frame_type);
    pos = common::EncodeVarint(pos, _sequence_number);

    buffer->MoveWritePt(pos - span.GetStart());
    return true;
}

bool RetireConnectionIDFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = common::FixedDecodeUint16(pos, end, _frame_type);
        if (_frame_type != FT_RETIRE_CONNECTION_ID) {
            return false;
        }
    }
    pos = common::DecodeVarint(pos, end, _sequence_number);

    buffer->MoveReadPt(pos - span.GetStart());
    return true;
}

uint32_t RetireConnectionIDFrame::EncodeSize() {
    return sizeof(RetireConnectionIDFrame);
}
  
}
}