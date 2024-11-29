#include "common/log/log.h"
#include "common/decode/decode.h"
#include "quic/frame/max_data_frame.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {
namespace quic {

MaxDataFrame::MaxDataFrame(): 
    IFrame(FT_MAX_DATA),
    maximum_data_(0) {

}

MaxDataFrame::~MaxDataFrame() {

}

bool MaxDataFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = span.GetStart();
    pos = common::FixedEncodeUint16(pos, frame_type_);
    pos = common::EncodeVarint(pos, maximum_data_);

    buffer->MoveWritePt(pos - span.GetStart());
    return true;
}

bool MaxDataFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = common::FixedDecodeUint16(pos, end, frame_type_);
        if (frame_type_ != FT_MAX_DATA) {
            return false;
        }
    }
    pos = common::DecodeVarint(pos, end, maximum_data_);

    buffer->MoveReadPt(pos - span.GetStart());
    return true;
}

uint32_t MaxDataFrame::EncodeSize() {
    return sizeof(MaxDataFrame);
}

}
}