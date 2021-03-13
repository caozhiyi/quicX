#include "ack_frame.h"
#include "common/decode/normal_decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {

AckFrame::AckFrame():
    Frame(FT_ACK),
    _largest_ack(0),
    _ack_delay(0),
    _first_ack_range(0) {

}

AckFrame::~AckFrame() {

}

bool AckFrame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();
    char* data = alloter->PoolMalloc<char>(size);
    char* pos = data;

    pos = EncodeFixed<uint16_t>(pos, _frame_type);
    pos = EncodeVarint(pos, _largest_ack);
    pos = EncodeVarint(pos, _ack_delay);
    pos = EncodeVarint(pos, _ack_ranges.size());
    pos = EncodeVarint(pos, _first_ack_range);

    for (size_t i = 0; i < _ack_ranges.size(); i++) {
        pos = EncodeVarint(pos, _ack_ranges[i]._gap);
        pos = EncodeVarint(pos, _ack_ranges[i]._ack_range);
    }
    
    buffer->Write(data, pos - data);
    alloter->PoolFree(data, size);
    return true;
}

bool AckFrame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    uint16_t size = EncodeSize();
    char* data = alloter->PoolMalloc<char>(size);
    buffer->ReadNotMovePt(data, size);
    
    char* pos = data;
    if (with_type) {
        pos = DecodeFixed<uint16_t>(data, data + size, _frame_type);
        if (_frame_type != FT_ACK && _frame_type != FT_ACK_ECN) {
            return false;
        }
    }
    pos = DecodeVirint(pos, data + size, _largest_ack);
    pos = DecodeVirint(pos, data + size, _ack_delay);
    uint32_t ack_range_count = 0;
    pos = DecodeVirint(pos, data + size, ack_range_count);
    pos = DecodeVirint(pos, data + size, _first_ack_range);

    buffer->MoveReadPt(pos - data);
    alloter->PoolFree(data, size);

    size = ack_range_count * sizeof(AckRange);
    data = alloter->PoolMalloc<char>(size);
    buffer->ReadNotMovePt(data, size);
    pos = data;

    uint32_t gap;
    uint32_t range;
    for (uint32_t i = 0; i < ack_range_count; i++) {
        pos = DecodeVirint(pos, data + size, gap);
        pos = DecodeVirint(pos, data + size, range);
        _ack_ranges.emplace_back(AckRange{gap, range});
    }
    buffer->MoveReadPt(pos - data);
    alloter->PoolFree(data, size);
    
    return true;
}

uint32_t AckFrame::EncodeSize() {
    return sizeof(AckFrame) + _ack_ranges.size() * sizeof(AckRange);
}

AckFrame::AckFrame(FrameType ft):
    Frame(ft),
    _largest_ack(0),
    _ack_delay(0),
    _first_ack_range(0) {

}


AckEcnFrame::AckEcnFrame():
    AckFrame(FT_ACK_ECN),
    _ect_0(0),
    _ect_1(0),
    _ecn_ce(0) {

}

AckEcnFrame::~AckEcnFrame() {

}

bool AckEcnFrame::AckEcnFrame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    if (!AckFrame::Encode(buffer, alloter)) {
        return false;
    }

    uint16_t size = EncodeSize() - sizeof(AckFrame);
    char* data = alloter->PoolMalloc<char>(size);
    char* pos = data;
    
    pos = EncodeVarint(pos, _ect_0);
    pos = EncodeVarint(pos, _ect_1);
    pos = EncodeVarint(pos, _ecn_ce);

    buffer->Write(data, pos - data);
    alloter->PoolFree(data, size);

    return true;
}

bool AckEcnFrame::AckEcnFrame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    if (!AckFrame::Decode(buffer, alloter, with_type)) {
        return false;
    } 

    uint16_t size = EncodeSize();
    char* data = alloter->PoolMalloc<char>(size);
    buffer->ReadNotMovePt(data, size);
    char* pos = data;

    pos = DecodeVirint(pos, data + size, _ect_0);
    pos = DecodeVirint(pos, data + size, _ect_1);
    pos = DecodeVirint(pos, data + size, _ecn_ce);

    buffer->MoveReadPt(pos - data);
    alloter->PoolFree(data, size);

    return true;
}

uint32_t AckEcnFrame::AckEcnFrame::EncodeSize() {
    return sizeof(AckEcnFrame) - sizeof(AckFrame) + AckFrame::EncodeSize();
}

}