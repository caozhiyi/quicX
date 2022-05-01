#include "common/log/log.h"
#include "quic/packet/type.h"
#include "quic/common/version.h"
#include "common/decode/decode.h"
#include "quic/common/constants.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/long_header.h"

namespace quicx {

InitPacket::InitPacket(std::shared_ptr<IHeader> header):
    IPacket(header) {

}

InitPacket::~InitPacket() {

}

bool InitPacket::Encode(std::shared_ptr<IBufferWriteOnly> buffer) {
    return true;
}

bool InitPacket::Decode(std::shared_ptr<IBufferReadOnly> buffer) {
    if (!_header) {
        LOG_ERROR("empty header.");
        return false;
    }

    // check version
    std::shared_ptr<LongHeader> header = std::dynamic_pointer_cast<LongHeader>(_header);
    if (!CheckVersion(header->GetVersion())) {
        return false;
    }
    
    auto pos_pair = buffer->GetReadPair();

    // check buffer length
    if (pos_pair.second - pos_pair.first <= __min_initial_size) {
        LOG_ERROR("buffer is too small for initial packet");
        return false;
    }
    
    char* pos = pos_pair.first;
    pos = DecodeVarint(pos, pos_pair.second, _token_length);
    _token = pos;
    pos += _token_length;
    LOG_DEBUG("get initial token:%s", std::string(_token, _token_length));

    pos = DecodeVarint(pos, pos_pair.second, _payload_length);
    _payload = pos;
    pos += _payload_length;

    _buffer = buffer;
    buffer->MoveReadPt(pos - pos_pair.first);
    return true;
}

uint32_t InitPacket::EncodeSize() {
    return 0;
}

bool InitPacket::AddFrame(std::shared_ptr<IFrame> frame) {
    return true;
}

}