#include "common/log/log.h"
#include "data_blocked_frame.h"
#include "common/decode/decode.h"
#include "common/buffer/if_buffer.h"
#include "common/alloter/if_alloter.h"

namespace quicx {
namespace quic {

DataBlockedFrame::DataBlockedFrame():
    IFrame(FT_DATA_BLOCKED),
    maximum_data_(0) {

}

DataBlockedFrame::~DataBlockedFrame() {

}

bool DataBlockedFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
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

bool DataBlockedFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = common::FixedDecodeUint16(pos, end, frame_type_);
        if (frame_type_ != FT_DATA_BLOCKED) {
            return false;
        }
    }
    pos = common::DecodeVarint(pos, end, maximum_data_);

    buffer->MoveReadPt(pos - span.GetStart());
    return true;
}

uint32_t DataBlockedFrame::EncodeSize() {
    return sizeof(DataBlockedFrame);
}

}
}
