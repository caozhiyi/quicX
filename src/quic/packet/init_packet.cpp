#include "common/log/log.h"
#include "quic/packet/type.h"
#include "quic/common/version.h"
#include "common/decode/decode.h"
#include "quic/common/constants.h"
#include "quic/packet/init_packet.h"

namespace quicx {

InitPacket::InitPacket() {

}

InitPacket::InitPacket(uint8_t flag):
    _header(flag) {

}

InitPacket::~InitPacket() {

}

bool InitPacket::Encode(std::shared_ptr<IBufferWrite> buffer) {
    return true;
}

bool InitPacket::Decode(std::shared_ptr<IBufferRead> buffer) {
    if (!_header.DecodeHeader(buffer)) {
        LOG_ERROR("decode header failed");
        return false;
    }

    // check version
    if (!CheckVersion(_header.GetVersion())) {
        return false;
    }
    
    auto pos_pair = buffer->GetReadPair();

    // check buffer length
    if (pos_pair.second - pos_pair.first <= __min_initial_size) {
        LOG_ERROR("buffer is too small for initial packet");
        return false;
    }
    
    const uint8_t* pos = pos_pair.first;
    pos = DecodeVarint(pos, pos_pair.second, _token_length);
    _token = pos;
    pos += _token_length;
    LOG_DEBUG("get initial token:%s", _token);

    pos = DecodeVarint(pos, pos_pair.second, _payload_length);
    //_payload.SetData(pos, _payload_length);
    pos += _payload_length;

    _packet_src_data = std::make_pair(pos_pair.first, pos);
    buffer->MoveReadPt(pos - pos_pair.first);
    return true;
}

uint32_t InitPacket::EncodeSize() {
    return 0;
}

bool InitPacket::AddFrame(std::shared_ptr<IFrame> frame) {
    return true;
}

//BufferReadView& InitPacket::GetPayload() {
    //return _payload;
//}

}