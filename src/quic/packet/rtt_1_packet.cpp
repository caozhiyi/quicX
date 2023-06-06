#include <cstring>
#include "common/log/log.h"
#include "quic/packet/type.h"
#include "common/buffer/buffer.h"
#include "quic/frame/frame_decode.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/packet/packet_number.h"
#include "common/buffer/buffer_read_view.h"

namespace quicx {

Rtt1Packet::Rtt1Packet() {

}

Rtt1Packet::Rtt1Packet(uint8_t flag):
    _header(flag) {

}

Rtt1Packet::~Rtt1Packet() {

}

bool Rtt1Packet::Encode(std::shared_ptr<IBufferWrite> buffer, std::shared_ptr<ICryptographer> crypto_grapher) {
    if (!_header.EncodeHeader(buffer)) {
        LOG_ERROR("encode header failed");
        return false;
    }

    auto span = buffer->GetWriteSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;
    uint8_t* end = span.GetEnd();

    // encode packet number
    cur_pos = PacketNumber::Encode(cur_pos, _header.GetPacketNumberLength(), _packet_number);

    // encode payload 
    if (!crypto_grapher) {
        _payload_offset = cur_pos - start_pos;
        memcpy(cur_pos, _payload.GetStart(), _payload.GetLength());
        cur_pos += _payload.GetLength();
        buffer->MoveWritePt(cur_pos - span.GetStart());
        return true;
    }

     // encode payload whit encrypt
    buffer->MoveWritePt(cur_pos - start_pos);
    auto header_span = _header.GetHeaderSrcData();
    if(!crypto_grapher->EncryptPacket(_packet_number, header_span, _payload, buffer)) {
        LOG_ERROR("encrypt payload failed.");
        return false;
    }

    BufferSpan sample = BufferSpan(start_pos + 4,
    start_pos + 4 + __header_protect_sample_length);
    if(!crypto_grapher->EncryptHeader(header_span, sample, header_span.GetLength(), _header.GetPacketNumberLength(),
        _header.GetHeaderType() == PHT_SHORT_HEADER)) {
        LOG_ERROR("encrypt header failed.");
        return false;
    }

    return true;
}

bool Rtt1Packet::Decode(std::shared_ptr<IBufferRead> buffer) {
    if (!_header.DecodeHeader(buffer)) {
        LOG_ERROR("decode header failed");
        return false;
    }

    // decode cipher data
    _packet_src_data = buffer->GetReadSpan();

    // move buffer read point
    buffer->MoveReadPt(_packet_src_data.GetLength());
    return true;
}

bool Rtt1Packet::Decode(std::shared_ptr<ICryptographer> crypto_grapher) {
    auto span = _packet_src_data;
    uint8_t* cur_pos = span.GetStart();
    uint8_t* end = span.GetEnd();
    
    if (!crypto_grapher) {
        // decrypt packet number
        cur_pos = PacketNumber::Decode(cur_pos, _header.GetPacketNumberLength(), _packet_number);

        // decode payload frames
        _payload = BufferSpan(cur_pos, end);
        std::shared_ptr<BufferReadView> view = std::make_shared<BufferReadView>(_payload.GetStart(), _payload.GetEnd());
        if(!DecodeFrames(view, _frame_list)) {
            LOG_ERROR("decode frame failed.");
            return false;
        }
        return true;
    }
    
    // decrypt header
    uint8_t packet_num_len = 0;
    BufferSpan header_span = _header.GetHeaderSrcData();
    BufferSpan sample = BufferSpan(span.GetStart() + 4,
        span.GetStart() + 4 + __header_protect_sample_length);
    if(!crypto_grapher->DecryptHeader(header_span, sample, header_span.GetLength(), packet_num_len, 
        _header.GetHeaderType() == PHT_SHORT_HEADER)) {
        LOG_ERROR("decrypt header failed.");
        return false;
    }
    _header.SetPacketNumberLength(packet_num_len);
    cur_pos = PacketNumber::Decode(cur_pos, packet_num_len, _packet_number);

    // decrypt packet
    uint8_t buf[1450] = {0};
    auto payload = BufferSpan(cur_pos, span.GetEnd());
    std::shared_ptr<IBuffer> out_plaintext = std::make_shared<Buffer>(buf, buf + 1450);
    if(!crypto_grapher->DecryptPacket(_packet_number, header_span, payload, out_plaintext)) {
        LOG_ERROR("decrypt packet failed.");
        return false;
    }

    if(!DecodeFrames(out_plaintext, _frame_list)) {
        LOG_ERROR("decode frame failed.");
        return false;
    }

    return true;
}

void Rtt1Packet::SetPayload(BufferSpan payload) {
    _payload = payload;
}

}
