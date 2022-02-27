#include "common/decode/decode.h"
#include "streams_blocked_frame.h"
#include "common/decode/normal_decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {


StreamsBlockedFrame::StreamsBlockedFrame(uint16_t frame_type):
    Frame(frame_type),
    _maximum_streams(0) {

}

StreamsBlockedFrame::~StreamsBlockedFrame() {

}

bool StreamsBlockedFrame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();
    char* data = alloter->PoolMalloc<char>(size);
    char* pos = data;

    pos = EncodeFixed<uint16_t>(pos, _frame_type);
    pos = EncodeVarint(pos, _maximum_streams);

    buffer->Write(data, pos - data);
    alloter->PoolFree(data, size);
    return true;
}

bool StreamsBlockedFrame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    uint16_t size = EncodeSize();
    char* data = alloter->PoolMalloc<char>(size);
    buffer->ReadNotMovePt(data, size);
    char* pos = data;

    if (with_type) {
        pos = DecodeFixed<uint16_t>(data, data + size, _frame_type);
        if (_frame_type != FT_STREAMS_BLOCKED_BIDIRECTIONAL && _frame_type != FT_STREAMS_BLOCKED_BIDIRECTIONAL) {
            return false;
        }
    }
    pos = DecodeVarint(pos, data + size, _maximum_streams);

    buffer->MoveReadPt(pos - data);
    alloter->PoolFree(data, size);
    return true;
}

uint32_t StreamsBlockedFrame::EncodeSize() {
    return sizeof(StreamsBlockedFrame);
}

}
