#include "common/log/log.h"
#include "quic/packet/type.h"
#include "common/decode/decode.h"
#include "quic/packet/header/header_flag.h"

namespace quicx {

bool HeaderFlag::EncodeFlag(std::shared_ptr<IBufferWrite> buffer) {
    uint16_t need_size = EncodeFlagSize();
    auto pos_pair = buffer->GetWritePair();
    auto remain_size = pos_pair.second - pos_pair.first;
    if (need_size > remain_size) {
        LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = pos_pair.first;
    pos = FixedEncodeUint8(pos, _flag._header_flag);
    buffer->MoveWritePt(pos - pos_pair.first);
    return true;
}

bool HeaderFlag::DecodeFlag(std::shared_ptr<IBufferRead> buffer) {
    auto pos_pair = buffer->GetReadPair();
    if (buffer->GetDataLength() < EncodeFlagSize()) {
        return false;
    }

    const uint8_t* pos = pos_pair.first;
    pos = FixedDecodeUint8(pos, pos_pair.second, _flag._header_flag);
    buffer->MoveReadPt(pos - pos_pair.first);
    return true;
}

uint32_t HeaderFlag::EncodeFlagSize() {
    return sizeof(uint8_t);
}

PacketHeaderType HeaderFlag::GetHeaderType() const {
    return _flag._long_header_flag._header_form != 1 ? PHT_LONG_HEADER : PHT_SHORT_HEADER;
}

PacketType HeaderFlag::GetPacketType() const {
    if (GetHeaderType() == PHT_SHORT_HEADER) {
        return PT_1RTT;
    }
    switch (GetLongHeaderFlag()._packet_type) {
    case 0x00:
        return PT_INITIAL;
    case 0x01:
        return PT_0RTT;
    case 0x02:
        return PT_HANDSHAKE;
    case 0x03:
        return PT_RETRY;
    case 0x04:
        return PT_NEGOTIATION;
    default:
        LOG_ERROR("unknow packet type. type:%d", GetLongHeaderFlag()._packet_type);
        break;
    }
    return PT_UNKNOW;
}

}