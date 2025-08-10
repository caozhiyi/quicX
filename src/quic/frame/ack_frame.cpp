#include "common/log/log.h"
#include "quic/frame/ack_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace quic {

AckFrame::AckFrame():
    IFrame(FrameType::kAck),
    ack_delay_(0) {

}

AckFrame::~AckFrame() {

}

bool AckFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }
    
    common::BufferEncodeWrapper wrapper(buffer);
    wrapper.EncodeFixedUint16(frame_type_);
    wrapper.EncodeVarint(ack_delay_);
    wrapper.EncodeVarint(first_ack_range_);
    wrapper.EncodeVarint(ack_ranges_.size());

    for (size_t i = 0; i < ack_ranges_.size(); i++) {
        wrapper.EncodeVarint(ack_ranges_[i].GetGap());
        wrapper.EncodeVarint(ack_ranges_[i].GetAckRangeLength());
    }
    return true;
}

bool AckFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);
    if (with_type) {
        wrapper.DecodeFixedUint16(frame_type_);
        if (frame_type_ != FrameType::kAck && frame_type_ != FrameType::kAckEcn) {
            common::LOG_ERROR("invalid frame type. frame_type:%d", frame_type_);
            return false;
        }
    }
    
    wrapper.DecodeVarint(ack_delay_);
    wrapper.DecodeVarint(first_ack_range_);
    uint32_t ack_range_count = 0;
    wrapper.DecodeVarint(ack_range_count);

    uint64_t gap;
    uint64_t range;
    for (uint32_t i = 0; i < ack_range_count; i++) {
        wrapper.DecodeVarint(gap);
        wrapper.DecodeVarint(range);
        ack_ranges_.emplace_back(AckRange(gap, range));
    }
    return true;
}

uint32_t AckFrame::EncodeSize() {
    return sizeof(AckFrame) + ack_ranges_.size() * sizeof(AckRange);
}

AckFrame::AckFrame(FrameType ft):
    IFrame(static_cast<uint16_t>(ft)),
    ack_delay_(0) {

}


AckEcnFrame::AckEcnFrame():
    AckFrame(static_cast<FrameType>(FrameType::kAckEcn)),
    ect_0_(0),
    ect_1_(0),
    ecn_ce_(0) {

}

AckEcnFrame::~AckEcnFrame() {

}

bool AckEcnFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    if (!AckFrame::Encode(buffer)) {
        return false;
    }

    uint16_t need_size = EncodeSize();
    
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }
    
    common::BufferEncodeWrapper wrapper(buffer);
    wrapper.EncodeVarint(ect_0_);
    wrapper.EncodeVarint(ect_1_);
    wrapper.EncodeVarint(ecn_ce_);

    return true;
}

bool AckEcnFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    if (!AckFrame::Decode(buffer, with_type)) {
        return false;
    } 

    auto span = buffer->GetReadSpan();

    common::BufferDecodeWrapper wrapper(buffer);
    wrapper.DecodeVarint(ect_0_);
    wrapper.DecodeVarint(ect_1_);
    wrapper.DecodeVarint(ecn_ce_);

    return true;
}

uint32_t AckEcnFrame::EncodeSize() {
    return sizeof(uint64_t) * 3;
}

}
}