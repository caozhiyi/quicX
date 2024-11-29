#include "common/log/log.h"
#include "quic/frame/ack_frame.h"
#include "common/decode/decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {
namespace quic {

AckFrame::AckFrame():
    IFrame(FT_ACK),
    ack_delay_(0) {

}

AckFrame::~AckFrame() {

}

bool AckFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }
    
    uint8_t* pos = span.GetStart();
    pos = common::FixedEncodeUint16(pos, frame_type_);
    pos = common::EncodeVarint(pos, ack_delay_);
    pos = common::EncodeVarint(pos, first_ack_range_);
    pos = common::EncodeVarint(pos, ack_ranges_.size());

    for (size_t i = 0; i < ack_ranges_.size(); i++) {
        pos = common::EncodeVarint(pos, ack_ranges_[i].GetGap());
        pos = common::EncodeVarint(pos, ack_ranges_[i].GetAckRangeLength());
    }
    
    buffer->MoveWritePt(pos - span.GetStart());
    return true;
}

bool AckFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();
    if (with_type) {
        pos = common::FixedDecodeUint16(pos, end, frame_type_);
        if (frame_type_ != FT_ACK && frame_type_ != FT_ACK_ECN) {
            return false;
        }
    }
    pos = common::DecodeVarint(pos, end, ack_delay_);
    pos = common::DecodeVarint(pos, end, first_ack_range_);
    uint32_t ack_range_count = 0;
    pos = common::DecodeVarint(pos, end, ack_range_count);

    uint64_t gap;
    uint64_t range;
    for (uint32_t i = 0; i < ack_range_count; i++) {
        pos = common::DecodeVarint(pos, end, gap);
        pos = common::DecodeVarint(pos, end, range);
        ack_ranges_.emplace_back(AckRange(gap, range));
    }
    buffer->MoveReadPt(pos - span.GetStart());
    return true;
}

uint32_t AckFrame::EncodeSize() {
    return sizeof(AckFrame) + ack_ranges_.size() * sizeof(AckRange);
}

AckFrame::AckFrame(FrameType ft):
    IFrame(ft),
    ack_delay_(0) {

}


AckEcnFrame::AckEcnFrame():
    AckFrame(FT_ACK_ECN),
    ect_0_(0),
    ect_1_(0),
    ecn_ce_(0) {

}

AckEcnFrame::~AckEcnFrame() {

}

bool AckEcnFrame::AckEcnFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
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
    
    uint8_t* pos = span.GetStart();
    pos = common::EncodeVarint(pos, ect_0_);
    pos = common::EncodeVarint(pos, ect_1_);
    pos = common::EncodeVarint(pos, ecn_ce_);

    buffer->MoveWritePt(pos - span.GetStart());

    return true;
}

bool AckEcnFrame::AckEcnFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    if (!AckFrame::Decode(buffer, with_type)) {
        return false;
    } 

    auto span = buffer->GetReadSpan();

    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();
    pos = common::DecodeVarint(pos, end, ect_0_);
    pos = common::DecodeVarint(pos, end, ect_1_);
    pos = common::DecodeVarint(pos, end, ecn_ce_);

    buffer->MoveReadPt(pos - span.GetStart());

    return true;
}

uint32_t AckEcnFrame::AckEcnFrame::EncodeSize() {
    return sizeof(uint64_t) * 3;
}

}
}