#include "common/log/log.h"
#include "quic/common/version.h"
#include "common/decode/decode.h"
#include "quic/common/constants.h"
#include "quic/packet/init_packet.h"

namespace quicx {

InitPacket::InitPacket():
    _packet_num_offset(0) {

}

InitPacket::InitPacket(uint8_t flag):
    _header(flag),
    _packet_num_offset(0) {

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

    auto span = buffer->GetReadSpan();

    // check buffer length
    if (span.GetLength() <= __min_initial_size) {
        LOG_ERROR("buffer is too small for initial packet");
        return false;
    }
    
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();
    pos = DecodeVarint(pos, end, _token_length);
    _token = pos;
    pos += _token_length;
    LOG_DEBUG("get initial token:%s", _token);

    _packet_num_offset = pos - span.GetStart();
    pos = DecodeVarint(pos, end, _payload_length);
    //_payload.SetData(pos, _payload_length);
    pos += _payload_length;
    

    _packet_src_data = std::move(BufferSpan(span.GetStart(), pos));
    buffer->MoveReadPt(pos - span.GetStart());
    return true;
}

uint32_t InitPacket::EncodeSize() {
    return 0;
}

bool InitPacket::AddFrame(std::shared_ptr<IFrame> frame) {
    return true;
}

uint32_t InitPacket::GetPacketNumOffset() {
    return _packet_num_offset;
}

//BufferReadView& InitPacket::GetPayload() {
    //return _payload;
//}

}