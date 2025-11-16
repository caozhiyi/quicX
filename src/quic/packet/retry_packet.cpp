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

bool RetryPacket::Encode(std::shared_ptr<common::IBuffer> buffer) {
    if (!header_.EncodeHeader(buffer)) {
        common::LOG_ERROR("encode header failed");
        return false;
    }

    auto span = buffer->GetWritableSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;

    // encode retry token
    if (retry_token_.Valid()) {
        std::memcpy(cur_pos, retry_token_.GetStart(), retry_token_.GetLength());
    cur_pos += retry_token_.GetLength();
    }

    // encode retry integrity tag 
    std::memcpy(cur_pos, retry_integrity_tag_, kRetryIntegrityTagLength);
    cur_pos += kRetryIntegrityTagLength;
    buffer->MoveWritePt(cur_pos - span.GetStart());
    return true;
}

bool RetryPacket::DecodeWithoutCrypto(std::shared_ptr<common::IBuffer> buffer, bool with_flag) {
    if (!header_.DecodeHeader(buffer, with_flag)) {
        common::LOG_ERROR("decode header failed");
        return false;
    }

    auto span = buffer->GetReadableSpan();
    auto shared_span = buffer->GetSharedReadableSpan();
    if (!shared_span.Valid()) {
        common::LOG_ERROR("readable span is invalid");
        return false;
    }
    auto chunk = shared_span.GetChunk();
    uint8_t* cur_pos = span.GetStart();
    uint32_t token_len = span.GetLength() - kRetryIntegrityTagLength;

    // decode retry token
    retry_token_ = common::SharedBufferSpan(chunk, span.GetStart(), span.GetStart() + token_len);
    cur_pos += token_len;

    // decode retry integrity tag 
    std::memcpy(retry_integrity_tag_, cur_pos, kRetryIntegrityTagLength);
    cur_pos += kRetryIntegrityTagLength;
    buffer->MoveReadPt(cur_pos - span.GetStart());
    return true;
}

void RetryPacket::SetRetryIntegrityTag(uint8_t* tag) {
    std::memcpy(retry_integrity_tag_, tag, kRetryIntegrityTagLength);
}

uint8_t* RetryPacket::GetRetryIntegrityTag() {
    return retry_integrity_tag_;
}

}
}
