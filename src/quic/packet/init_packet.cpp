#include <cstring>

#include "common/buffer/buffer_chunk.h"
#include "common/buffer/single_block_buffer.h"
#include "common/decode/decode.h"
#include "common/log/log.h"

#include "quic/frame/frame_decode.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/packet_number.h"
#include "quic/quicx/global_resource.h"

namespace quicx {
namespace quic {

InitPacket::InitPacket():
    payload_offset_(0),
    packet_num_offset_(0),
    token_length_(0) {
    header_.GetLongHeaderFlag().SetPacketType(PacketType::kInitialPacketType);
}

InitPacket::InitPacket(uint8_t flag):
    header_(flag),
    payload_offset_(0),
    packet_num_offset_(0),
    token_length_(0) {}

InitPacket::~InitPacket() {}

bool InitPacket::Encode(std::shared_ptr<common::IBuffer> buffer) {
    if (!header_.EncodeHeader(buffer)) {
        common::LOG_ERROR("encode header failed");
        return false;
    }

    auto span = buffer->GetWritableSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;
    uint8_t* end = span.GetEnd();

    // encode token
    cur_pos = common::EncodeVarint(cur_pos, end, token_length_);
    if (token_length_ > 0) {
        std::memcpy(cur_pos, token_, token_length_);
        cur_pos += token_length_;
    }

    // encode length
    auto len = payload_.GetLength();
    auto len1 = header_.GetPacketNumberLength();
    auto len2 = crypto_grapher_ ? crypto_grapher_->GetTagLength() : 0;
    length_ = payload_.GetLength() + header_.GetPacketNumberLength() +
              (crypto_grapher_ ? crypto_grapher_->GetTagLength() : 0);
    cur_pos = common::EncodeVarint(cur_pos, end, length_);

    // encode packet number
    packet_num_offset_ = cur_pos - start_pos;
    cur_pos = PacketNumber::Encode(cur_pos, header_.GetPacketNumberLength(), packet_number_);

    // encode payload
    if (!crypto_grapher_) {
        payload_offset_ = cur_pos - start_pos;
        std::memcpy(cur_pos, payload_.GetStart(), payload_.GetLength());
        cur_pos += payload_.GetLength();
        buffer->MoveWritePt(cur_pos - span.GetStart());
        return true;
    }

    // encode payload whit encrypt
    buffer->MoveWritePt(cur_pos - start_pos);

    auto header_span1 = header_.GetHeaderSrcData().GetSpan();
    // RFC 9001 §5.3: AD = header + token + length + packet_number (from header start to cur_pos)
    auto ad_span = common::BufferSpan(header_span1.GetStart(), cur_pos);

    auto payload_span = payload_.GetSpan();
    auto result = crypto_grapher_->EncryptPacket(packet_number_, ad_span, payload_span, buffer);
    if (result != ICryptographer::Result::kOk) {
        common::LOG_ERROR("encrypt payload failed. result:%d", result);
        return false;
    }

    auto header_span = header_.GetHeaderSrcData().GetSpan();
    common::BufferSpan sample = common::BufferSpan(
        start_pos + packet_num_offset_ + 4, start_pos + packet_num_offset_ + 4 + kHeaderProtectSampleLength);
    result = crypto_grapher_->EncryptHeader(header_span, sample, header_span.GetLength() + packet_num_offset_,
        header_.GetPacketNumberLength(), header_.GetHeaderType() == PacketHeaderType::kShortHeader);
    if (result != ICryptographer::Result::kOk) {
        common::LOG_ERROR("encrypt header failed. result:%d", result);
        return false;
    }

    return true;
}

bool InitPacket::DecodeWithoutCrypto(std::shared_ptr<common::IBuffer> buffer, bool with_flag) {
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
    uint8_t* end = span.GetEnd();

    // decode token
    cur_pos = common::DecodeVarint(cur_pos, end, token_length_);
    if (cur_pos == nullptr) {
        common::LOG_ERROR("InitPacket: failed to decode token length");
        return false;
    }
    token_ = cur_pos;
    cur_pos += token_length_;

    // decode length
    cur_pos = common::DecodeVarint(cur_pos, end, length_);
    if (cur_pos == nullptr) {
        common::LOG_ERROR("InitPacket: failed to decode length field");
        return false;
    }

    // decode cipher data
    packet_num_offset_ = cur_pos - span.GetStart();
    cur_pos += length_;

    // set src data
    packet_src_data_ = common::SharedBufferSpan(chunk, span.GetStart(), cur_pos);

    // move buffer read point
    buffer->MoveReadPt(cur_pos - span.GetStart());
    return true;
}

bool InitPacket::DecodeWithCrypto(std::shared_ptr<common::IBuffer> buffer) {
    auto span = packet_src_data_;
    uint8_t* cur_pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (!crypto_grapher_) {
        // decrypt packet number
        cur_pos += packet_num_offset_;
        cur_pos = PacketNumber::Decode(cur_pos, header_.GetPacketNumberLength(), packet_number_);

        // decode payload frames
        payload_ = common::SharedBufferSpan(
            packet_src_data_.GetChunk(), cur_pos, cur_pos + length_ - header_.GetPacketNumberLength());

        // Create a SingleBlockBuffer from the payload span
        auto payload_buffer = common::SingleBlockBuffer::FromSpan(payload_);
        if (!payload_buffer) {
            common::LOG_ERROR("failed to create buffer from payload span.");
            return false;
        }

        if (!DecodeFrames(payload_buffer, frames_list_)) {
            common::LOG_ERROR("decode frame failed.");
            return false;
        }
        return true;
    }

    // decrypt header
    uint8_t packet_num_len = 0;
    auto header_span = header_.GetHeaderSrcData().GetSpan();

    // get decrypt sample, which is defined in RFC9001 §5.4.2
    common::BufferSpan sample = common::BufferSpan(span.GetStart() + packet_num_offset_ + 4,
        span.GetStart() + packet_num_offset_ + 4 + kHeaderProtectSampleLength);

    auto result = crypto_grapher_->DecryptHeader(header_span, sample, header_span.GetLength() + packet_num_offset_,
        packet_num_len, header_.GetHeaderType() == PacketHeaderType::kShortHeader);
    if (result != ICryptographer::Result::kOk) {
        common::LOG_ERROR("decrypt header failed. result:%d", result);
        return false;
    }

    // RFC 9001 §5.3: Copy decrypted header back to buffer for AD construction
    auto header_len = header_span.GetLength();
    uint8_t* buffer_header_pos = span.GetStart() - header_len;
    memcpy(buffer_header_pos, header_span.GetStart(), header_len);

    header_.SetPacketNumberLength(packet_num_len);
    cur_pos += packet_num_offset_;
    cur_pos = PacketNumber::Decode(cur_pos, packet_num_len, packet_number_);

    // RFC 9001 §5.3: AD includes header (from first byte) up to and including the unprotected PN
    auto ad_span = common::BufferSpan(buffer_header_pos, cur_pos);

    // decrypt packet
    auto payload = common::BufferSpan(cur_pos, cur_pos + length_ - packet_num_len);
    // Create a separate buffer for decrypted plaintext to avoid garbage data (header/length) from original buffer
    // Use pooled BufferChunk instead of StandaloneBufferChunk for memory reuse
    auto chunk = std::make_shared<common::BufferChunk>(GlobalResource::Instance().GetThreadLocalBlockPool());
    auto plaintext_buffer = std::make_shared<common::SingleBlockBuffer>(chunk);

    result = crypto_grapher_->DecryptPacket(packet_number_, ad_span, payload, plaintext_buffer);
    if (result != ICryptographer::Result::kOk) {
        common::LOG_ERROR("decrypt packet failed. result:%d", result);
        return false;
    }
    // Read frames from the decrypted plaintext buffer
    if (!DecodeFrames(plaintext_buffer, frames_list_)) {
        common::LOG_ERROR("decode frame failed.");
        return false;
    }

    // Set frame_type_bit based on decoded frames for ACK tracking
    for (const auto& frame : frames_list_) {
        frame_type_bit_ |= (1 << frame->GetType());
    }

    return true;
}

void InitPacket::SetToken(uint8_t* token, uint32_t len) {
    token_ = token;
    token_length_ = len;
}

void InitPacket::SetPayload(const common::SharedBufferSpan& payload) {
    payload_ = payload;
}

}  // namespace quic
}  // namespace quicx