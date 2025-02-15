
#include "common/log/log.h"
#include "quic/packet/type.h"
#include "common/decode/decode.h"
#include "common/buffer/buffer.h"
#include "quic/frame/frame_decode.h"
#include "quic/packet/packet_number.h"
#include "quic/packet/handshake_packet.h"
#include "common/buffer/buffer_read_view.h"

namespace quicx {
namespace quic {

HandshakePacket::HandshakePacket() {
    header_.GetLongHeaderFlag().SetPacketType(PacketType::kHandshakePacketType);
}

HandshakePacket::HandshakePacket(uint8_t flag):
    header_(flag) {

}

HandshakePacket::~HandshakePacket() {

}

bool HandshakePacket::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    if (!header_.EncodeHeader(buffer)) {
        common::LOG_ERROR("encode header failed");
        return false;
    }

    auto span = buffer->GetWriteSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;
    uint8_t* end = span.GetEnd();

    // encode length
    length_ = payload_.GetLength() + header_.GetPacketNumberLength() + (crypto_grapher_ ? crypto_grapher_->GetTagLength() : 0);
    cur_pos = common::EncodeVarint(cur_pos, end, length_);

    // encode packet number
    packet_num_offset_ = cur_pos - start_pos;
    cur_pos = PacketNumber::Encode(cur_pos, header_.GetPacketNumberLength(), packet_number_);

    // encode payload 
    if (!crypto_grapher_) {
        payload_offset_ = cur_pos - start_pos;
        memcpy(cur_pos, payload_.GetStart(), payload_.GetLength());
        cur_pos += payload_.GetLength();
        buffer->MoveWritePt(cur_pos - span.GetStart());
        return true;
    }

     // encode payload whit encrypt
    buffer->MoveWritePt(cur_pos - start_pos);
    auto header_span = header_.GetHeaderSrcData();
    if(!crypto_grapher_->EncryptPacket(packet_number_, header_span, payload_, buffer)) {
        common::LOG_ERROR("encrypt payload failed.");
        return false;
    }

    // get encrypt sample, which is defined in RFC9001
    common::BufferSpan sample = common::BufferSpan(start_pos + packet_num_offset_ + 4,
    start_pos + packet_num_offset_ + 4 + kHeaderProtectSampleLength);
    if(!crypto_grapher_->EncryptHeader(header_span, sample, header_span.GetLength() + packet_num_offset_, header_.GetPacketNumberLength(),
        header_.GetHeaderType() == PacketHeaderType::kShortHeader)) {
        common::LOG_ERROR("encrypt header failed.");
        return false;
    }

    return true;
}

bool HandshakePacket::DecodeWithoutCrypto(std::shared_ptr<common::IBufferRead> buffer) {
    if (!header_.DecodeHeader(buffer)) {
        common::LOG_ERROR("decode header failed");
        return false;
    }

    auto span = buffer->GetReadSpan();
    uint8_t* cur_pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    // decode length
    cur_pos = common::DecodeVarint(cur_pos, end, length_);

    // decode cipher data
    packet_num_offset_ = cur_pos - span.GetStart();
    cur_pos += length_;

    // set src data
    packet_src_data_ = std::move(common::BufferSpan(span.GetStart(), cur_pos));

    // move buffer read point
    buffer->MoveReadPt(cur_pos - span.GetStart());
    return true;
}


bool HandshakePacket::DecodeWithCrypto(std::shared_ptr<common::IBuffer> buffer) {
    auto span = packet_src_data_;
    uint8_t* cur_pos = span.GetStart();
    uint8_t* end = span.GetEnd();
    
    if (!crypto_grapher_) {
        // decrypt packet number
        cur_pos += packet_num_offset_;
        cur_pos = PacketNumber::Decode(cur_pos, header_.GetPacketNumberLength(), packet_number_);

        // decode payload frames
        payload_ = common::BufferSpan(cur_pos, cur_pos + length_ - header_.GetPacketNumberLength());
        std::shared_ptr<common::BufferReadView> view = std::make_shared<common::BufferReadView>(payload_.GetStart(), payload_.GetEnd());
        if(!DecodeFrames(view, frames_list_)) {
            common::LOG_ERROR("decode frame failed.");
            return false;
        }
        return true;
    }
    
    // decrypt header
    uint8_t packet_num_len = 0;
    common::BufferSpan header_span = header_.GetHeaderSrcData();
    // get decrypt sample, which is defined in RFC9001
    common::BufferSpan sample = common::BufferSpan(span.GetStart() + packet_num_offset_ + 4,
        span.GetStart() + packet_num_offset_ + 4 + kHeaderProtectSampleLength);
    if(!crypto_grapher_->DecryptHeader(header_span, sample, header_span.GetLength() + packet_num_offset_, packet_num_len, 
        header_.GetHeaderType() == PacketHeaderType::kShortHeader)) {
        common::LOG_ERROR("decrypt header failed.");
        return false;
    }
    header_.SetPacketNumberLength(packet_num_len);
    cur_pos += packet_num_offset_;
    cur_pos = PacketNumber::Decode(cur_pos, packet_num_len, packet_number_);

    // decrypt packet
    auto payload = common::BufferSpan(cur_pos, cur_pos + length_ - packet_num_len);
    if(!crypto_grapher_->DecryptPacket(packet_number_, header_span, payload, buffer)) {
        common::LOG_ERROR("decrypt packet failed.");
        return false;
    }

    if(!DecodeFrames(buffer, frames_list_)) {
        common::LOG_ERROR("decode frame failed.");
        return false;
    }

    return true;
}

void HandshakePacket::SetPayload(common::BufferSpan payload) {
    payload_ = payload;
}

}
}