#include "connection_close_frame.h"
#include "common/decode/normal_decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {

ConnectionCloseFrame::ConnectionCloseFrame():
    Frame(FT_CONNECTION_CLOSE),
    _error_code(0),
    _err_frame_type(0) {

};

ConnectionCloseFrame::~ConnectionCloseFrame() {

}

bool ConnectionCloseFrame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();
    char* data = alloter->PoolMalloc<char>(size);
    char* pos = data;
    pos = EncodeFixed<uint16_t>(data, _frame_type);
    pos = EncodeVarint(pos, _error_code);
    pos = EncodeVarint(pos, _err_frame_type);
    pos = EncodeVarint(pos, _reason.length());

    buffer->Write(data, pos - data);
    buffer->Write(_reason.c_str(), _reason.length());

    alloter->PoolFree(data, size);
    return true;
}

bool ConnectionCloseFrame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);
    buffer->ReadNotMovePt(data, size);
    uint64_t reason_length = 0;
    char* pos = data;

    if (with_type) {
        pos = DecodeFixed<uint16_t>(pos, data + size, _frame_type);
        if (_frame_type != FT_CONNECTION_CLOSE) {
            return false;
        }
    }
    pos = DecodeVirint(pos, data + size, _error_code);
    pos = DecodeVirint(pos, data + size, _err_frame_type);
    pos = DecodeVirint(pos, data + size, reason_length);
    
    buffer->MoveReadPt(pos - data);
    alloter->PoolFree(data, size);

    data = alloter->PoolMalloc<char>(reason_length);
    if (buffer->Read(data, reason_length) != reason_length) {
        return false;
    }
    _reason.clear();
    _reason.append(data, reason_length);
    alloter->PoolFree(data, reason_length);
    
    return true;
}

uint32_t ConnectionCloseFrame::EncodeSize() {
    return sizeof(ConnectionCloseFrame);
}

}