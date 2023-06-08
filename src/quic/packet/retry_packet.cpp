#include <cstring>
#include "common/log/log.h"
#include "quic/packet/type.h"
#include "quic/packet/retry_packet.h"

namespace quicx {

RetryPacket::RetryPacket() {
    _header.GetLongHeaderFlag().SetPacketType(PT_RETRY);
}

RetryPacket::RetryPacket(uint8_t flag):
    _header(flag) {

}

RetryPacket::~RetryPacket() {

}

bool RetryPacket::Encode(std::shared_ptr<IBufferWrite> buffer, std::shared_ptr<ICryptographer> crypto_grapher) {
    if (!_header.EncodeHeader(buffer)) {
        LOG_ERROR("encode header failed");
        return false;
    }

    auto span = buffer->GetWriteSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;

    // encode retry token
    memcpy(cur_pos, _retry_token.GetStart(), _retry_token.GetLength());
    cur_pos += _retry_token.GetLength();

    // encode retry integrity tag 
    memcpy(cur_pos, _retry_integrity_tag, __retry_integrity_tag_length);
    cur_pos += __retry_integrity_tag_length;
    buffer->MoveWritePt(cur_pos - span.GetStart());
    return true;
}

bool RetryPacket::Decode(std::shared_ptr<IBufferRead> buffer) {
    if (!_header.DecodeHeader(buffer)) {
        LOG_ERROR("decode header failed");
        return false;
    }

    auto span = buffer->GetReadSpan();
    uint8_t* cur_pos = span.GetStart();
    uint32_t token_len = span.GetLength() - __retry_integrity_tag_length;

    // decode retry token
    _retry_token = std::move(BufferSpan(span.GetStart(), token_len));
    cur_pos += token_len;

    // decode retry integrity tag 
    memcpy(_retry_integrity_tag, cur_pos, __retry_integrity_tag_length);
    cur_pos += __retry_integrity_tag_length;
    buffer->MoveReadPt(cur_pos - span.GetStart());
    return true;
}

void RetryPacket::SetRetryIntegrityTag(uint8_t* tag) {
    memcpy(_retry_integrity_tag, tag, __retry_integrity_tag_length);
}

uint8_t* RetryPacket::GetRetryIntegrityTag() {
    return _retry_integrity_tag;
}

}
