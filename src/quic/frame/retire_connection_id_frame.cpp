#include "common/log/log.h"
#include "common/decode/decode.h"
#include "common/buffer/if_buffer.h"
#include "common/alloter/if_alloter.h"
#include "quic/frame/retire_connection_id_frame.h"

namespace quicx {
namespace quic {

RetireConnectionIDFrame::RetireConnectionIDFrame():
    IFrame(FT_RETIRE_CONNECTION_ID),
    sequence_number_(0) {

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
    pos = common::FixedEncodeUint16(pos, frame_type_);
    pos = common::EncodeVarint(pos, sequence_number_);

    buffer->MoveWritePt(pos - span.GetStart());
    return true;
}

bool RetireConnectionIDFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = common::FixedDecodeUint16(pos, end, frame_type_);
        if (frame_type_ != FT_RETIRE_CONNECTION_ID) {
            return false;
        }
    }
    pos = common::DecodeVarint(pos, end, sequence_number_);

    buffer->MoveReadPt(pos - span.GetStart());
    return true;
}

uint32_t RetireConnectionIDFrame::EncodeSize() {
    return sizeof(RetireConnectionIDFrame);
}
  
}
}