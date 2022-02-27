#include "common/decode/decode.h"
#include "stream_data_blocked_frame.h"
#include "common/decode/normal_decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {

StreamDataBlockedFrame::StreamDataBlockedFrame():
    Frame(FT_STREAM_DATA_BLOCKED),
    _stream_id(0),
    _maximum_data(0) {

}

StreamDataBlockedFrame::~StreamDataBlockedFrame() {

}

bool StreamDataBlockedFrame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();
    char* data = alloter->PoolMalloc<char>(size);
    char* pos = data;

    pos = EncodeFixed<uint16_t>(pos, _frame_type);
    pos = EncodeVarint(pos, _stream_id);
    pos = EncodeVarint(pos, _maximum_data);

    buffer->Write(data, pos - data);
    alloter->PoolFree(data, size);
    return true;
}

bool StreamDataBlockedFrame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    uint16_t size = EncodeSize();
    char* data = alloter->PoolMalloc<char>(size);
    buffer->ReadNotMovePt(data, size);
    char* pos = data;

    if (with_type) {
        pos = DecodeFixed<uint16_t>(data, data + size, _frame_type);
        if (_frame_type != FT_STREAM_DATA_BLOCKED) {
            return false;
        }
    }
    pos = DecodeVarint(pos, data + size, _stream_id);
    pos = DecodeVarint(pos, data + size, _maximum_data);

    buffer->MoveReadPt(pos - data);
    alloter->PoolFree(data, size);
    return true;
}

uint32_t StreamDataBlockedFrame::EncodeSize() {
    return sizeof(StreamDataBlockedFrame);
}

}