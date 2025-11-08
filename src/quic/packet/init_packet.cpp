#include "common/log/log.h"
#include "quic/common/version.h"
#include "common/buffer/buffer.h"
#include "common/decode/decode.h"
#include "quic/common/constants.h"
#include "quic/packet/init_packet.h"
#include "quic/frame/frame_decode.h"
#include "quic/packet/packet_number.h"
#include "common/buffer/buffer_read_view.h"

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
    token_length_(0) {

}

InitPacket::~InitPacket() {

}

bool InitPacket::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    if (!header_.EncodeHeader(buffer)) {
        common::LOG_ERROR("encode header failed");
        return false;
    }

    auto span = buffer->GetWriteSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;
    uint8_t* end = span.GetEnd();

    // encode token
    cur_pos = common::EncodeVarint(cur_pos, end, token_length_);
    if (token_length_ > 0) {
        memcpy(cur_pos, token_, token_length_);
        cur_pos += token_length_;
    }

    // encode length
    auto len = payload_.GetLength();
    auto len1 = header_.GetPacketNumberLength();
    auto len2 = crypto_grapher_ ? crypto_grapher_->GetTagLength() : 0;
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
    
    auto result = crypto_grapher_->EncryptPacket(packet_number_, header_span, payload_, buffer);
    if(result != ICryptographer::Result::kOk) {
        common::LOG_ERROR("encrypt payload failed. result:%d", result);
        return false;
    }

    common::BufferSpan sample = common::BufferSpan(start_pos + packet_num_offset_ + 4,
    start_pos + packet_num_offset_ + 4 + kHeaderProtectSampleLength);
    result = crypto_grapher_->EncryptHeader(header_span, sample, header_span.GetLength() + packet_num_offset_, header_.GetPacketNumberLength(),
        header_.GetHeaderType() == PacketHeaderType::kShortHeader);
    if(result != ICryptographer::Result::kOk) {
        common::LOG_ERROR("encrypt header failed. result:%d", result);
        return false;
    }

    return true;
}

bool InitPacket::DecodeWithoutCrypto(std::shared_ptr<common::IBufferRead> buffer, bool with_flag) {
    if (!header_.DecodeHeader(buffer, with_flag)) {
        common::LOG_ERROR("decode header failed");
        return false;
    }

    auto span = buffer->GetReadSpan();
    uint8_t* cur_pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    // decode token
    cur_pos = common::DecodeVarint(cur_pos, end, token_length_);
    token_ = cur_pos;
    cur_pos += token_length_;
    if (token_length_ > 0) {
        common::LOG_DEBUG("get initial token:%s", token_);
    }
    
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

bool InitPacket::DecodeWithCrypto(std::shared_ptr<common::IBuffer> buffer) {
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
    common::BufferSpan sample = common::BufferSpan(span.GetStart() + packet_num_offset_ + 4,
        span.GetStart() + packet_num_offset_ + 4 + kHeaderProtectSampleLength);
    auto result = crypto_grapher_->DecryptHeader(header_span, sample, header_span.GetLength() + packet_num_offset_, packet_num_len, 
        header_.GetHeaderType() == PacketHeaderType::kShortHeader);
    if(result != ICryptographer::Result::kOk) {
        common::LOG_ERROR("decrypt header failed. result:%d", result);
        return false;
    }
    header_.SetPacketNumberLength(packet_num_len);
    cur_pos += packet_num_offset_;
    cur_pos = PacketNumber::Decode(cur_pos, packet_num_len, packet_number_);

    // decrypt packet
    auto payload = common::BufferSpan(cur_pos, cur_pos + length_ - packet_num_len);
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

void InitPacket::SetToken(uint8_t* token, uint32_t len) {
    token_ = token;
    token_length_ = len;
}

void InitPacket::SetPayload(common::BufferSpan payload) {
    payload_ = payload;
}

}
}