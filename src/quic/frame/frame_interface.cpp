#include "common/log/log.h"
#include "common/decode/decode.h"
#include "quic/frame/frame_interface.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {

IFrame::IFrame(uint16_t ft): 
    _frame_type(ft) {
    
}

IFrame::~IFrame() {

}

uint16_t IFrame::GetType() { 
    return _frame_type; 
}

bool IFrame::Encode(std::shared_ptr<IBufferWriteOnly> buffer) {
    uint16_t need_size = EncodeSize();
    
    auto pos_pair = buffer->GetWritePair();
    auto remain_size = pos_pair.second - pos_pair.first;

    if (need_size > remain_size) {
        LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }
    
    char* pos = FixedEncodeUint16(pos_pair.first, _frame_type);
    buffer->MoveWritePt(uint32_t(pos - pos_pair.first));

    return true;
}

bool IFrame::Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type) {
    if (with_type) {
        auto pos_pair = buffer->GetReadPair();
        char* pos = FixedDecodeUint16(pos_pair.first, pos_pair.second, _frame_type);
        buffer->MoveReadPt(uint32_t(pos - pos_pair.first));
    }
    return true;
}

uint32_t IFrame::EncodeSize() {
    return sizeof(uint16_t);
}

}