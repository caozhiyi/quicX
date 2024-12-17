#include "common/log/log.h"
#include "quic/frame/if_frame.h"
#include "quic/frame/stream_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace quic {

IFrame::IFrame(uint16_t ft): 
    frame_type_(ft) {
    
}

IFrame::~IFrame() {

}

uint16_t IFrame::GetType() { 
    return frame_type_; 
}

bool IFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }
    
    common::BufferEncodeWrapper wrapper(buffer);
    wrapper.EncodeFixedUint16(frame_type_);

    return true;
}

bool IFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    if (with_type) {
        common::BufferDecodeWrapper wrapper(buffer);
        wrapper.DecodeFixedUint16(frame_type_);
    }
    return true;
}

uint32_t IFrame::EncodeSize() {
    return sizeof(uint16_t);
}

uint32_t IFrame::GetFrameTypeBit() {
    if (StreamFrame::IsStreamFrame(frame_type_)) {
        return FTB_STREAM;
    }
    
    return (uint32_t)1 << frame_type_;
}

}
}