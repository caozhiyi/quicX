#include <cstring>

#include "common/buffer/buffer_chunk.h"
#include "common/buffer/single_block_buffer.h"
#include "common/log/log.h"

#include "quic/crypto/type.h"
#include "quic/frame/frame_decode.h"
#include "quic/packet/packet_number.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/quicx/global_resource.h"

namespace quicx {
namespace quic {

Rtt1Packet::Rtt1Packet() {}

Rtt1Packet::Rtt1Packet(uint8_t flag):
    header_(flag) {}

Rtt1Packet::~Rtt1Packet() {}

bool Rtt1Packet::Encode(std::shared_ptr<common::IBuffer> buffer) {
    if (!header_.EncodeHeader(buffer)) {
        common::LOG_ERROR("encode header failed");
        return false;
    }

    auto span = buffer->GetWritableSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;

    // encode packet number
    cur_pos = PacketNumber::Encode(cur_pos, header_.GetPacketNumberLength(), packet_number_);

    // encode payload
    if (!crypto_grapher_) {
        payload_offset_ = cur_pos - start_pos;
        if (payload_.Valid()) {
            std::memcpy(cur_pos, payload_.GetStart(), payload_.GetLength());
            cur_pos += payload_.GetLength();
        }
        buffer->MoveWritePt(cur_pos - span.GetStart());
        return true;
    }

    // encode payload with encrypt
    buffer->MoveWritePt(cur_pos - start_pos);
    auto header_span = header_.GetHeaderSrcData().GetSpan();
    // RFC 9001 §5.3: AD = header + packet_number (from header start to cur_pos)
    // For Short Header: AD = [Flag][DCID][PN]
    auto ad_span = common::BufferSpan(header_span.GetStart(), cur_pos);
    auto payload_span = payload_.GetSpan();
    auto result = crypto_grapher_->EncryptPacket(packet_number_, ad_span, payload_span, buffer);
    if (result != ICryptographer::Result::kOk) {
        common::LOG_ERROR("encrypt payload failed. result:%d", result);
        return false;
    }

    // get encrypt sample, which is defined in RFC9001 §5.4.2
    common::BufferSpan sample = common::BufferSpan(start_pos + 4, start_pos + 4 + kHeaderProtectSampleLength);
    result = crypto_grapher_->EncryptHeader(header_span, sample, header_span.GetLength(),
        header_.GetPacketNumberLength(), header_.GetHeaderType() == PacketHeaderType::kShortHeader);
    if (result != ICryptographer::Result::kOk) {
        common::LOG_ERROR("encrypt header failed. result:%d", result);
        return false;
    }

    return true;
}

bool Rtt1Packet::DecodeWithoutCrypto(std::shared_ptr<common::IBuffer> buffer, bool with_flag) {
    if (!header_.DecodeHeader(buffer, with_flag)) {
        common::LOG_ERROR("decode header failed");
        return false;
    }

    // decode cipher data
    auto span = buffer->GetReadableSpan();
    auto shared_span = buffer->GetSharedReadableSpan();
    if (!shared_span.Valid()) {
        common::LOG_ERROR("readable span is invalid");
        return false;
    }
    packet_src_data_ = common::SharedBufferSpan(shared_span.GetChunk(), span.GetStart(), span.GetEnd());

    // move buffer read point
    buffer->MoveReadPt(span.GetLength());
    return true;
}

bool Rtt1Packet::DecodeWithCrypto(std::shared_ptr<common::IBuffer> buffer) {
    auto span = packet_src_data_;
    uint8_t* cur_pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (!crypto_grapher_) {
        // RFC 9000 Appendix A: Two-step packet number recovery
        uint64_t truncated_pn = 0;
        cur_pos = PacketNumber::Decode(cur_pos, header_.GetPacketNumberLength(), truncated_pn);
        packet_number_ = PacketNumber::Decode(largest_received_pn_, truncated_pn, header_.GetPacketNumberLength() * 8);

        // decode payload frames
        payload_ = common::SharedBufferSpan(packet_src_data_.GetChunk(), cur_pos, end);

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
    // For short header: sample starts at pn_offset + 4 (pn_offset = 0 since PN follows header directly)
    // Sample needs 16 bytes, starting at offset 4 from payload start
    size_t payload_len = span.GetEnd() - span.GetStart();
    if (payload_len < 4 + kHeaderProtectSampleLength) {
        common::LOG_ERROR("payload too short for header protection sample. payload_len:%zu, required:%zu",
            payload_len, 4 + kHeaderProtectSampleLength);
        return false;
    }
    common::BufferSpan sample =
        common::BufferSpan(span.GetStart() + 4, span.GetStart() + 4 + kHeaderProtectSampleLength);
    auto result = crypto_grapher_->DecryptHeader(header_span, sample, header_span.GetLength(), packet_num_len,
        header_.GetHeaderType() == PacketHeaderType::kShortHeader);
    if (result != ICryptographer::Result::kOk) {
        common::LOG_ERROR("decrypt header failed. result:%d, payload_len:%zu, header_len:%zu", 
            result, payload_len, header_span.GetLength());
        return false;
    }

    // RFC 9001 §6: Extract Key Phase bit from the decrypted short header flag
    // After header protection removal, bit 2 of the first byte contains the key_phase
    uint8_t decrypted_flag = *(header_span.GetStart());
    ShortHeaderFlag* shf = reinterpret_cast<ShortHeaderFlag*>(&decrypted_flag);
    uint8_t received_key_phase = shf->GetKeyPhase();
    bool key_phase_changed = (received_key_phase != expected_key_phase_);

    // RFC 9001 §5.3: Copy decrypted header back to buffer for AD construction
    // The header_span points to decrypted header in header_src_data_.
    // We need to copy it back to the buffer so AD can be constructed from contiguous memory.
    auto header_len = header_span.GetLength();
    uint8_t* buffer_header_pos = span.GetStart() - header_len;
    memcpy(buffer_header_pos, header_span.GetStart(), header_len);

    header_.SetPacketNumberLength(packet_num_len);
    // RFC 9000 Appendix A: Two-step packet number recovery
    // Step 1: Read the truncated PN bytes from the wire
    uint64_t truncated_pn = 0;
    cur_pos = PacketNumber::Decode(cur_pos, packet_num_len, truncated_pn);
    // Step 2: Recover full PN using largest received PN
    packet_number_ = PacketNumber::Decode(largest_received_pn_, truncated_pn, packet_num_len * 8);

    // RFC 9001 §5.3: AD includes header (from first byte) up to and including the unprotected PN
    // For Short Header: AD = [Flag][DCID][PN]
    auto ad_span = common::BufferSpan(buffer_header_pos, cur_pos);

    // decrypt packet payload
    auto payload = common::BufferSpan(cur_pos, span.GetEnd());
    // Use pooled BufferChunk instead of StandaloneBufferChunk for memory reuse
    auto chunk = std::make_shared<common::BufferChunk>(GlobalResource::Instance().GetThreadLocalBlockPool());
    auto plaintext_buffer = std::make_shared<common::SingleBlockBuffer>(chunk);

    if (!key_phase_changed) {
        // Key Phase matches: try decrypt with current key
        result = crypto_grapher_->DecryptPacket(packet_number_, ad_span, payload, plaintext_buffer);
        if (result != ICryptographer::Result::kOk) {
            // Current key failed, maybe this is a reordered packet from previous key phase
            if (crypto_grapher_->HasPrevReadKey()) {
                // Reset plaintext buffer for retry
                chunk = std::make_shared<common::BufferChunk>(GlobalResource::Instance().GetThreadLocalBlockPool());
                plaintext_buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
                result = crypto_grapher_->DecryptPacketWithPrevKey(packet_number_, ad_span, payload, plaintext_buffer);
                if (result != ICryptographer::Result::kOk) {
                    common::LOG_ERROR("decrypt packet failed with both current and prev key. result:%d, pn:%llu",
                        result, packet_number_);
                    return false;
                }
                // Decrypted with previous key - this is a reordered old packet, no key update needed
            } else {
                common::LOG_ERROR("decrypt packet failed. result:%d, pn:%llu, truncated_pn:%llu, pn_len:%u, largest_recv_pn:%llu, "
                    "payload_len:%zu, ad_len:%zu, header_len:%zu",
                    result, packet_number_, truncated_pn, packet_num_len, largest_received_pn_,
                    (size_t)(span.GetEnd() - cur_pos), (size_t)(cur_pos - buffer_header_pos), header_len);
                return false;
            }
        }
    } else {
        // RFC 9001 §6: Key Phase changed - peer has performed a Key Update
        // Save state for retry after connection layer rotates keys
        saved_payload_start_ = cur_pos;
        saved_payload_end_ = span.GetEnd();
        saved_ad_start_ = buffer_header_pos;
        saved_header_len_ = header_len;
        saved_truncated_pn_ = truncated_pn;
        key_phase_changed_ = true;
        common::LOG_INFO("Key Phase changed (expected:%u, received:%u) at pn:%llu, signaling key update",
            expected_key_phase_, received_key_phase, packet_number_);
        return false;
    }

    if (!DecodeFrames(plaintext_buffer, frames_list_)) {
        common::LOG_ERROR("decode frame failed.");
        return false;
    }

    // Set frame_type_bit based on decoded frames for ACK tracking
    for (const auto& frame : frames_list_) {
        frame_type_bit_ |= (1u << frame->GetType());
    }

    return true;
}

void Rtt1Packet::SetPayload(const common::SharedBufferSpan& payload) {
    payload_ = payload;
}

bool Rtt1Packet::RetryPayloadDecrypt() {
    if (!crypto_grapher_ || !saved_payload_start_ || !saved_ad_start_) {
        common::LOG_ERROR("RetryPayloadDecrypt called without saved state");
        return false;
    }

    // Reconstruct AD and payload spans from saved state
    auto ad_span = common::BufferSpan(saved_ad_start_, saved_payload_start_);
    auto payload = common::BufferSpan(saved_payload_start_, saved_payload_end_);

    // Allocate new plaintext buffer
    auto chunk = std::make_shared<common::BufferChunk>(GlobalResource::Instance().GetThreadLocalBlockPool());
    auto plaintext_buffer = std::make_shared<common::SingleBlockBuffer>(chunk);

    auto result = crypto_grapher_->DecryptPacket(packet_number_, ad_span, payload, plaintext_buffer);
    if (result != ICryptographer::Result::kOk) {
        common::LOG_ERROR("RetryPayloadDecrypt: decrypt still failed after key update. result:%d, pn:%llu",
            result, packet_number_);
        return false;
    }

    if (!DecodeFrames(plaintext_buffer, frames_list_)) {
        common::LOG_ERROR("RetryPayloadDecrypt: decode frame failed.");
        return false;
    }

    // Set frame_type_bit based on decoded frames for ACK tracking
    for (const auto& frame : frames_list_) {
        frame_type_bit_ |= (1u << frame->GetType());
    }

    // Clear saved state
    saved_payload_start_ = nullptr;
    saved_ad_start_ = nullptr;
    saved_payload_end_ = nullptr;

    return true;
}

}  // namespace quic
}  // namespace quicx
