#include "common/log/log.h"
#include "quic/frame/ack_frame.h"
#include "common/decode/decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {

AckFrame::AckFrame():
    IFrame(FT_ACK),
    _ack_delay(0) {

}

AckFrame::~AckFrame() {

}

bool AckFrame::Encode(std::shared_ptr<IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }
    
    uint8_t* pos = span.GetStart();
    pos = FixedEncodeUint16(pos, _frame_type);
    pos = EncodeVarint(pos, _ack_delay);
    pos = EncodeVarint(pos, _first_ack_range);
    pos = EncodeVarint(pos, _ack_ranges.size());

    for (size_t i = 0; i < _ack_ranges.size(); i++) {
        pos = EncodeVarint(pos, _ack_ranges[i].GetGap());
        pos = EncodeVarint(pos, _ack_ranges[i].GetAckRangeLength());
    }
    
    buffer->MoveWritePt(pos - span.GetStart());
    return true;
}

bool AckFrame::Decode(std::shared_ptr<IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();
    if (with_type) {
        pos = FixedDecodeUint16(pos, end, _frame_type);
        if (_frame_type != FT_ACK && _frame_type != FT_ACK_ECN) {
            return false;
        }
    }
    pos = DecodeVarint(pos, end, _ack_delay);
    pos = DecodeVarint(pos, end, _first_ack_range);
    uint32_t ack_range_count = 0;
    pos = DecodeVarint(pos, end, ack_range_count);

    uint64_t gap;
    uint64_t range;
    for (uint32_t i = 0; i < ack_range_count; i++) {
        pos = DecodeVarint(pos, end, gap);
        pos = DecodeVarint(pos, end, range);
        _ack_ranges.emplace_back(AckRange(gap, range));
    }
    buffer->MoveReadPt(pos - span.GetStart());
    return true;
}

uint32_t AckFrame::EncodeSize() {
    return sizeof(AckFrame) + _ack_ranges.size() * sizeof(AckRange);
}

AckFrame::AckFrame(FrameType ft):
    IFrame(ft),
    _ack_delay(0) {

}


AckEcnFrame::AckEcnFrame():
    AckFrame(FT_ACK_ECN),
    _ect_0(0),
    _ect_1(0),
    _ecn_ce(0) {

}

AckEcnFrame::~AckEcnFrame() {

}

bool AckEcnFrame::AckEcnFrame::Encode(std::shared_ptr<IBufferWrite> buffer) {
    if (!AckFrame::Encode(buffer)) {
        return false;
    }

    uint16_t need_size = EncodeSize();
    
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }
    
    uint8_t* pos = span.GetStart();
    pos = EncodeVarint(pos, _ect_0);
    pos = EncodeVarint(pos, _ect_1);
    pos = EncodeVarint(pos, _ecn_ce);

    buffer->MoveWritePt(pos - span.GetStart());

    return true;
}

bool AckEcnFrame::AckEcnFrame::Decode(std::shared_ptr<IBufferRead> buffer, bool with_type) {
    if (!AckFrame::Decode(buffer, with_type)) {
        return false;
    } 

    auto span = buffer->GetReadSpan();

    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();
    pos = DecodeVarint(pos, end, _ect_0);
    pos = DecodeVarint(pos, end, _ect_1);
    pos = DecodeVarint(pos, end, _ecn_ce);

    buffer->MoveReadPt(pos - span.GetStart());

    return true;
}

uint32_t AckEcnFrame::AckEcnFrame::EncodeSize() {
    return sizeof(uint64_t) * 3;
}

}