#include "common/log/log.h"
#include "common/decode/decode.h"
#include "common/buffer/if_buffer.h"
#include "common/alloter/if_alloter.h"
#include "quic/frame/streams_blocked_frame.h"

namespace quicx {
namespace quic {


StreamsBlockedFrame::StreamsBlockedFrame(uint16_t frame_type):
    IFrame(frame_type),
    maximum_streams_(0) {

}

StreamsBlockedFrame::~StreamsBlockedFrame() {

}

bool StreamsBlockedFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = span.GetStart();
    pos = common::FixedEncodeUint16(pos, frame_type_);
    pos = common::EncodeVarint(pos, maximum_streams_);

    buffer->MoveWritePt(pos - span.GetStart());
    return true;
}

bool StreamsBlockedFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = common::FixedDecodeUint16(pos, end, frame_type_);
        if (frame_type_ != FT_STREAMS_BLOCKED_BIDIRECTIONAL && frame_type_ != FT_STREAMS_BLOCKED_BIDIRECTIONAL) {
            return false;
        }
    }
    pos = common::DecodeVarint(pos, end, maximum_streams_);

    buffer->MoveReadPt(pos - span.GetStart());
    return true;
}

uint32_t StreamsBlockedFrame::EncodeSize() {
    return sizeof(StreamsBlockedFrame);
}

}
}
