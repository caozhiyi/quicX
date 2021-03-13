#include "new_token_frame.h"
#include "common/buffer/buffer_queue.h"
#include "common/decode/normal_decode.h"

namespace quicx {

NewTokenFrame::NewTokenFrame():
    Frame(FT_NEW_TOKEN) {

}

NewTokenFrame::~NewTokenFrame() {

}

bool NewTokenFrame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();
    char* data = alloter->PoolMalloc<char>(size);
    char* pos = data;

    pos = EncodeFixed<uint16_t>(pos, _frame_type);
    pos = EncodeVarint(pos, _token->GetCanReadLength());

    buffer->Write(data, pos - data);
    alloter->PoolFree(data, size);

    buffer->Write(_token);
    return true;
}

bool NewTokenFrame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    uint16_t size = EncodeSize();
    char* data = alloter->PoolMalloc<char>(size);
    buffer->ReadNotMovePt(data, size);
    char* pos = data;

    if (with_type) {
        pos = DecodeFixed<uint16_t>(data, data + size, _frame_type);
        if (_frame_type != FT_NEW_TOKEN) {
            return false;
        }
    }
    uint32_t length = 0;
    pos = DecodeVirint(pos, data + size, length);

    buffer->MoveReadPt(pos - data);
    alloter->PoolFree(data, size);

    _token = std::make_shared<BufferQueue>(buffer->GetBlockMemoryPool(), alloter);
    if (buffer->Read(_token, length) != length) {
        return false;
    }
    return true;
}

uint32_t NewTokenFrame::EncodeSize() {
    return sizeof(NewTokenFrame);
}

}