#include "crypto_frame.h"
#include "common/buffer/buffer_queue.h"
#include "common/decode/normal_decode.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {

CryptoFrame::CryptoFrame():
    Frame(FT_CRYPTO),
    _offset(0) {

}

CryptoFrame::~CryptoFrame() {

}

bool CryptoFrame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);

    char* pos = EncodeFixed<uint16_t>(data, _frame_type);
    pos = EncodeVarint(pos, _offset);
    pos = EncodeVarint(pos, _data->GetCanReadLength());

    buffer->Write(data, pos - data);
    alloter->PoolFree(data, size);

    buffer->Write(_data);
    return true;
}

bool CryptoFrame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);
    buffer->ReadNotMovePt(data, size);
    
    char* pos = data;
    if (with_type) {
        pos = DecodeFixed<uint16_t>(data, data + size, _frame_type);
    }
    pos = DecodeVirint(pos, data + size, _offset);
    uint32_t length = 0;
    pos = DecodeVirint(pos, data + size, length);

    buffer->MoveReadPt(pos - data);
    alloter->PoolFree(data, size);

    _data = std::make_shared<BufferQueue>(buffer->GetBlockMemoryPool(), alloter);
    buffer->Read(_data, length);
    return true;
}

uint32_t CryptoFrame::EncodeSize() {
    return sizeof(CryptoFrame);
}

}