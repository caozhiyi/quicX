#include <cstring>
#include "common/log/log.h"
#include "quic/packet/type.h"
#include "quic/packet/retry_packet.h"

namespace quicx {
namespace quic {

RetryPacket::RetryPacket() {
    header_.GetLongHeaderFlag().SetPacketType(PacketType::kRetryPacketType);
}

RetryPacket::RetryPacket(uint8_t flag):
    header_(flag) {

}

RetryPacket::~RetryPacket() {

}

bool RetryPacket::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    if (!header_.EncodeHeader(buffer)) {
        common::LOG_ERROR("encode header failed");
        return false;
    }

    auto span = buffer->GetWriteSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;

    // encode retry token
    memcpy(cur_pos, retry_token_.GetStart(), retry_token_.GetLength());
    cur_pos += retry_token_.GetLength();

    // encode retry integrity tag 
    memcpy(cur_pos, retry_integrity_tag_, kRetryIntegrityTagLength);
    cur_pos += kRetryIntegrityTagLength;
    buffer->MoveWritePt(cur_pos - span.GetStart());
    return true;
}

bool RetryPacket::DecodeWithoutCrypto(std::shared_ptr<common::IBufferRead> buffer) {
    if (!header_.DecodeHeader(buffer)) {
        common::LOG_ERROR("decode header failed");
        return false;
    }

    auto span = buffer->GetReadSpan();
    uint8_t* cur_pos = span.GetStart();
    uint32_t token_len = span.GetLength() - kRetryIntegrityTagLength;

    // decode retry token
    retry_token_ = std::move(common::BufferSpan(span.GetStart(), token_len));
    cur_pos += token_len;

    // decode retry integrity tag 
    memcpy(retry_integrity_tag_, cur_pos, kRetryIntegrityTagLength);
    cur_pos += kRetryIntegrityTagLength;
    buffer->MoveReadPt(cur_pos - span.GetStart());
    return true;
}

void RetryPacket::SetRetryIntegrityTag(uint8_t* tag) {
    memcpy(retry_integrity_tag_, tag, kRetryIntegrityTagLength);
}

uint8_t* RetryPacket::GetRetryIntegrityTag() {
    return retry_integrity_tag_;
}

}
}
