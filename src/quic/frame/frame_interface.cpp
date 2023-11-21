#include "common/log/log.h"
#include "common/decode/decode.h"
#include "quic/frame/stream_frame.h"
#include "quic/frame/frame_interface.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {
namespace quic {

IFrame::IFrame(uint16_t ft): 
    _frame_type(ft) {
    
}

IFrame::~IFrame() {

}

uint16_t IFrame::GetType() { 
    return _frame_type; 
}

bool IFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();

    if (need_size > remain_size) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }
    
    uint8_t* pos = common::FixedEncodeUint16(span.GetStart(), _frame_type);
    buffer->MoveWritePt(uint32_t(pos - span.GetStart()));

    return true;
}

bool IFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    if (with_type) {
        auto span = buffer->GetReadSpan();
        const uint8_t* pos = common::FixedDecodeUint16(span.GetStart(), span.GetEnd(), _frame_type);
        buffer->MoveReadPt(uint32_t(pos - span.GetStart()));
    }
    return true;
}

uint32_t IFrame::EncodeSize() {
    return sizeof(uint16_t);
}

uint32_t IFrame::GetFrameTypeBit() {
    if (StreamFrame::IsStreamFrame(_frame_type)) {
        return FTB_STREAM;
    }
    
    return (uint32_t)1 << _frame_type;
}

}
}