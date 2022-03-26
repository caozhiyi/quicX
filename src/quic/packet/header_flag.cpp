#include "common/log/log.h"
#include "quic/packet/header_flag.h"
#include "common/decode/normal_decode.h"

namespace quicx {

bool HeaderFlag::Encode(std::shared_ptr<IBufferWriteOnly> buffer) {
    uint16_t need_size = EncodeSize();
    auto pos_pair = buffer->GetWritePair();
    auto remain_size = pos_pair.second - pos_pair.first;
    if (need_size > remain_size) {
        LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    char* pos = pos_pair.first;
    pos = EncodeFixed<uint8_t>(pos, _flag._header_flag);
    buffer->MoveWritePt(pos - pos_pair.first);
    return true;
}

bool HeaderFlag::Decode(std::shared_ptr<IBufferReadOnly> buffer) {
    auto pos_pair = buffer->GetReadPair();
    if (buffer->GetCanReadLength() < EncodeSize()) {
        return false;
    }

    char* pos = pos_pair.first;
    pos = DecodeFixed<uint8_t>(pos, pos_pair.second, _flag._header_flag);
    buffer->MoveReadPt(pos - pos_pair.first);
    return true;
}

uint32_t HeaderFlag::EncodeSize() {
    return sizeof(uint8_t);
}

bool HeaderFlag::IsShortHeaderFlag() const {
    if (_flag._long_header_flag._header_form != 1) {
        return true;
    
    } else {
        return false;
    }
}

}