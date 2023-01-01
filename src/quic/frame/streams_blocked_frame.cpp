#include "common/log/log.h"
#include "common/decode/decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"
#include "quic/frame/streams_blocked_frame.h"

namespace quicx {


StreamsBlockedFrame::StreamsBlockedFrame(uint16_t frame_type):
    IFrame(frame_type),
    _maximum_streams(0) {

}

StreamsBlockedFrame::~StreamsBlockedFrame() {

}

bool StreamsBlockedFrame::Encode(std::shared_ptr<IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    auto pos_pair = buffer->GetWritePair();
    auto remain_size = pos_pair.second - pos_pair.first;
    if (need_size > remain_size) {
        LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = pos_pair.first;
    pos = FixedEncodeUint16(pos, _frame_type);
    pos = EncodeVarint(pos, _maximum_streams);

    buffer->MoveWritePt(pos - pos_pair.first);
    return true;
}

bool StreamsBlockedFrame::Decode(std::shared_ptr<IBufferRead> buffer, bool with_type) {
    auto pos_pair = buffer->GetReadPair();
    uint8_t* pos = pos_pair.first;

    if (with_type) {
        pos = FixedDecodeUint16(pos, pos_pair.second, _frame_type);
        if (_frame_type != FT_STREAMS_BLOCKED_BIDIRECTIONAL && _frame_type != FT_STREAMS_BLOCKED_BIDIRECTIONAL) {
            return false;
        }
    }
    pos = DecodeVarint(pos, pos_pair.second, _maximum_streams);

    buffer->MoveReadPt(pos - pos_pair.first);
    return true;
}

uint32_t StreamsBlockedFrame::EncodeSize() {
    return sizeof(StreamsBlockedFrame);
}

}
