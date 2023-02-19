#include "common/log/log.h"
#include "quic/common/version.h"
#include "common/decode/decode.h"
#include "quic/common/constants.h"
#include "quic/packet/init_packet.h"
#include "quic/frame/frame_decode.h"
#include "common/buffer/buffer_read_view.h"

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

bool InitPacket::DecodeBeforeDecrypt(std::shared_ptr<IBufferRead> buffer) {
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

    pos = DecodeVarint(pos, end, _payload_length);
    _packet_num_offset = pos - span.GetStart();

    pos += _payload_length;
    buffer->MoveReadPt(pos - span.GetStart());
    return true;
}

bool InitPacket::DecodeAfterDecrypt(std::shared_ptr<IBufferRead> buffer) {
    // decode payload frames
    if(!DecodeFrames(buffer, _frame_list)) {
        LOG_ERROR("decode frame failed.");
        return false;
    }

    return true;
}

uint32_t InitPacket::EncodeSize() {
    return 0;
}

bool InitPacket::AddFrame(std::shared_ptr<IFrame> frame) {
    return true;
}

}