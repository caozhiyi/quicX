#include <cstring>
#include "common/log/log.h"
#include "quic/packet/type.h"
#include "common/buffer/buffer.h"
#include "quic/frame/frame_decode.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/packet/packet_number.h"
#include "common/buffer/buffer_read_view.h"

namespace quicx {
namespace quic {

Rtt1Packet::Rtt1Packet() {

}

Rtt1Packet::Rtt1Packet(uint8_t flag):
    header_(flag) {

}

Rtt1Packet::~Rtt1Packet() {

}

bool Rtt1Packet::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    if (!header_.EncodeHeader(buffer)) {
        common::LOG_ERROR("encode header failed");
        return false;
    }

    auto span = buffer->GetWriteSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;

    // encode packet number
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

    common::BufferSpan sample = common::BufferSpan(start_pos + 4,
    start_pos + 4 + kHeaderProtectSampleLength);
    if(!crypto_grapher_->EncryptHeader(header_span, sample, header_span.GetLength(), header_.GetPacketNumberLength(),
        header_.GetHeaderType() == PHT_SHORT_HEADER)) {
        common::LOG_ERROR("encrypt header failed.");
        return false;
    }

    return true;
}

bool Rtt1Packet::DecodeWithoutCrypto(std::shared_ptr<common::IBufferRead> buffer) {
    if (!header_.DecodeHeader(buffer)) {
        common::LOG_ERROR("decode header failed");
        return false;
    }

    // decode cipher data
    packet_src_data_ = buffer->GetReadSpan();

    // move buffer read point
    buffer->MoveReadPt(packet_src_data_.GetLength());
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
        payload_ = common::BufferSpan(cur_pos, end);
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
    common::BufferSpan sample = common::BufferSpan(span.GetStart() + 4,
        span.GetStart() + 4 + kHeaderProtectSampleLength);
    if(!crypto_grapher_->DecryptHeader(header_span, sample, header_span.GetLength(), packet_num_len, 
        header_.GetHeaderType() == PHT_SHORT_HEADER)) {
        common::LOG_ERROR("decrypt header failed.");
        return false;
    }
    header_.SetPacketNumberLength(packet_num_len);
    cur_pos = PacketNumber::Decode(cur_pos, packet_num_len, packet_number_);

    // decrypt packet
    auto payload = common::BufferSpan(cur_pos, span.GetEnd());
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

void Rtt1Packet::SetPayload(common::BufferSpan payload) {
    payload_ = payload;
}

}
}
