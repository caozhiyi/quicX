#include "common/log/log.h"
#include "quic/packet/type.h"
#include "common/decode/decode.h"
#include "quic/packet/header/header_flag.h"

namespace quicx {
namespace quic {

HeaderFlag::HeaderFlag() { 
    _flag._header_flag = 0;
    _flag._long_header_flag._fix_bit = 1;
}

HeaderFlag::HeaderFlag(PacketHeaderType type) {
    _flag._header_flag = 0;
    _flag._long_header_flag._header_form = type;
    _flag._long_header_flag._fix_bit = 1;
}

HeaderFlag::HeaderFlag(uint8_t flag) {
    _flag._header_flag = flag;
}

bool HeaderFlag::EncodeFlag(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeFlagSize();
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = span.GetStart();
    pos = common::FixedEncodeUint8(pos, _flag._header_flag);
    buffer->MoveWritePt(pos - span.GetStart());
    return true;
}

bool HeaderFlag::DecodeFlag(std::shared_ptr<common::IBufferRead> buffer) {
    auto span = buffer->GetReadSpan();
    if (buffer->GetDataLength() < EncodeFlagSize()) {
        return false;
    }

    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();
    pos = common::FixedDecodeUint8(pos, end, _flag._header_flag);
    buffer->MoveReadPt(pos - span.GetStart());
    return true;
}

uint32_t HeaderFlag::EncodeFlagSize() {
    return sizeof(uint8_t);
}

PacketHeaderType HeaderFlag::GetHeaderType() const {
    return _flag._long_header_flag._header_form == 1 ? PHT_LONG_HEADER : PHT_SHORT_HEADER;
}

PacketType HeaderFlag::GetPacketType() {
    if (GetHeaderType() == PHT_SHORT_HEADER) {
        return PT_1RTT;
    }
    switch (GetLongHeaderFlag().GetPacketType()) {
    case 0x00:
        return PT_INITIAL;
    case 0x01:
        return PT_0RTT;
    case 0x02:
        return PT_HANDSHAKE;
    case 0x03:
        return PT_RETRY;
    default:
        common::LOG_ERROR("unknow packet type. type:%d", GetLongHeaderFlag()._packet_type);
        break;
    }
    return PT_UNKNOW;
}

}
}