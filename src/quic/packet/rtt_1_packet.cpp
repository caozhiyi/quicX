#include <cstring>
#include "common/log/log.h"
#include "quic/frame/frame_decode.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/packet/packet_number.h"
#include "common/buffer/single_block_buffer.h"

namespace quicx {
namespace quic {

Rtt1Packet::Rtt1Packet() {

}

Rtt1Packet::Rtt1Packet(uint8_t flag):
    header_(flag) {

}

Rtt1Packet::~Rtt1Packet() {

}

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

    // encode payload whit encrypt
    buffer->MoveWritePt(cur_pos - start_pos);
    auto header_span = header_.GetHeaderSrcData().GetSpan();
    auto payload_span = payload_.GetSpan();
    auto result = crypto_grapher_->EncryptPacket(packet_number_, header_span, payload_span, buffer);
    if(result != ICryptographer::Result::kOk) {
        common::LOG_ERROR("encrypt payload failed. result:%d", result);
        return false;
    }

    common::BufferSpan sample = common::BufferSpan(start_pos + 4,
    start_pos + 4 + kHeaderProtectSampleLength);
    result = crypto_grapher_->EncryptHeader(header_span, sample, header_span.GetLength(), header_.GetPacketNumberLength(),
        header_.GetHeaderType() == PacketHeaderType::kShortHeader);
    if(result != ICryptographer::Result::kOk) {
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
        // decrypt packet number
        cur_pos = PacketNumber::Decode(cur_pos, header_.GetPacketNumberLength(), packet_number_);

        // decode payload frames
        payload_ = common::SharedBufferSpan(packet_src_data_.GetChunk(), cur_pos, end);
        std::shared_ptr<common::SingleBlockBuffer> buffer = std::make_shared<common::SingleBlockBuffer>(payload_.GetChunk());
        if(!DecodeFrames(buffer, frames_list_)) {
            common::LOG_ERROR("decode frame failed.");
            return false;
        }
        return true;
    }
    
    // decrypt header
    uint8_t packet_num_len = 0;
    auto header_span = header_.GetHeaderSrcData().GetSpan();
    common::BufferSpan sample = common::BufferSpan(span.GetStart() + 4,
        span.GetStart() + 4 + kHeaderProtectSampleLength);
    auto result = crypto_grapher_->DecryptHeader(header_span, sample, header_span.GetLength(), packet_num_len, 
        header_.GetHeaderType() == PacketHeaderType::kShortHeader);
    if(result != ICryptographer::Result::kOk) {
        common::LOG_ERROR("decrypt header failed. result:%d", result);
        return false;
    }
    header_.SetPacketNumberLength(packet_num_len);
    cur_pos = PacketNumber::Decode(cur_pos, packet_num_len, packet_number_);

    // decrypt packet
    auto payload = common::BufferSpan(cur_pos, span.GetEnd());
    result = crypto_grapher_->DecryptPacket(packet_number_, header_span, payload, buffer);
    if(result != ICryptographer::Result::kOk) {
        common::LOG_ERROR("decrypt packet failed. result:%d", result);
        return false;
    }

    if(!DecodeFrames(buffer, frames_list_)) {
        common::LOG_ERROR("decode frame failed.");
        return false;
    }
    
    // Set frame_type_bit based on decoded frames for ACK tracking
    for (const auto& frame : frames_list_) {
        frame_type_bit_ |= (1 << frame->GetType());
    }

    return true;
}

void Rtt1Packet::SetPayload(const common::SharedBufferSpan& payload) {
    payload_ = payload;
}

}
}
