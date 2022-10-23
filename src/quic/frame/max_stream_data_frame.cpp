#include "common/log/log.h"
#include "common/decode/decode.h"
#include "common/buffer/buffer_interface.h"
#include "quic/frame/max_stream_data_frame.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {


MaxStreamDataFrame::MaxStreamDataFrame():
    IFrame(FT_MAX_STREAM_DATA),
    _stream_id(0),
    _maximum_data(0) {

}

MaxStreamDataFrame::~MaxStreamDataFrame() {

}

bool MaxStreamDataFrame::Encode(std::shared_ptr<IBufferWriteOnly> buffer) {
    uint16_t need_size = EncodeSize();
    auto pos_pair = buffer->GetWritePair();
    auto remain_size = pos_pair.second - pos_pair.first;
    if (need_size > remain_size) {
        LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = pos_pair.first;
    pos = FixedEncodeUint16(pos, _frame_type);
    pos = EncodeVarint(pos, _stream_id);
    pos = EncodeVarint(pos, _maximum_data);

    buffer->MoveWritePt(pos - pos_pair.first);
    return true;
}

bool MaxStreamDataFrame::Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type) {
    auto pos_pair = buffer->GetReadPair();
    uint8_t* pos = pos_pair.first;

    if (with_type) {
        pos = FixedDecodeUint16(pos, pos_pair.second, _frame_type);
        if (_frame_type != FT_MAX_STREAM_DATA) {
            return false;
        }
    }
    pos = DecodeVarint(pos, pos_pair.second, _stream_id);
    pos = DecodeVarint(pos, pos_pair.second, _maximum_data);

    buffer->MoveReadPt(pos - pos_pair.first);
    return true;
}

uint32_t MaxStreamDataFrame::EncodeSize() {
    return sizeof(MaxStreamDataFrame);
}

}