
#include "common/log/log.h"
#include "quic/packet/type.h"
#include "common/decode/decode.h"
#include "common/buffer/buffer.h"
#include "quic/frame/frame_decode.h"
#include "quic/packet/packet_number.h"
#include "quic/packet/handshake_packet.h"
#include "common/buffer/buffer_read_view.h"

namespace quicx {

HandshakePacket::HandshakePacket() {
    _header.GetLongHeaderFlag().SetPacketType(PT_HANDSHAKE);
}

HandshakePacket::HandshakePacket(uint8_t flag):
    _header(flag) {

}

HandshakePacket::~HandshakePacket() {

}

bool HandshakePacket::Encode(std::shared_ptr<IBufferWrite> buffer, std::shared_ptr<ICryptographer> crypto_grapher) {
    if (!_header.EncodeHeader(buffer)) {
        LOG_ERROR("encode header failed");
        return false;
    }

    auto span = buffer->GetWriteSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;
    uint8_t* end = span.GetEnd();

    // encode length
    _length = _payload.GetLength() + _header.GetPacketNumberLength() + (crypto_grapher ? crypto_grapher->GetTagLength() : 0);
    cur_pos = EncodeVarint(cur_pos, _length);

    // encode packet number
    _packet_num_offset = cur_pos - start_pos;
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

    BufferSpan sample = BufferSpan(start_pos + _packet_num_offset + 4,
    start_pos + _packet_num_offset + 4 + __header_protect_sample_length);
    if(!crypto_grapher->EncryptHeader(header_span, sample, header_span.GetLength() + _packet_num_offset, _header.GetPacketNumberLength(),
        _header.GetHeaderType() == PHT_SHORT_HEADER)) {
        LOG_ERROR("encrypt header failed.");
        return false;
    }

    return true;
}

bool HandshakePacket::Decode(std::shared_ptr<IBufferRead> buffer) {
    if (!_header.DecodeHeader(buffer)) {
        LOG_ERROR("decode header failed");
        return false;
    }

    auto span = buffer->GetReadSpan();
    uint8_t* cur_pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    // decode length
    cur_pos = DecodeVarint(cur_pos, end, _length);

    // decode cipher data
    _packet_num_offset = cur_pos - span.GetStart();
    cur_pos += _length;

    // set src data
    _packet_src_data = std::move(BufferSpan(span.GetStart(), cur_pos));

    // move buffer read point
    buffer->MoveReadPt(cur_pos - span.GetStart());
    return true;
}


bool HandshakePacket::Decode(std::shared_ptr<ICryptographer> crypto_grapher) {
    auto span = _packet_src_data;
    uint8_t* cur_pos = span.GetStart();
    uint8_t* end = span.GetEnd();
    
    if (!crypto_grapher) {
        // decrypt packet number
        cur_pos += _packet_num_offset;
        cur_pos = PacketNumber::Decode(cur_pos, _header.GetPacketNumberLength(), _packet_number);

        // decode payload frames
        _payload = BufferSpan(cur_pos, cur_pos + _length - _header.GetPacketNumberLength());
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
    BufferSpan sample = BufferSpan(span.GetStart() + _packet_num_offset + 4,
        span.GetStart() + _packet_num_offset + 4 + __header_protect_sample_length);
    if(!crypto_grapher->DecryptHeader(header_span, sample, header_span.GetLength() + _packet_num_offset, packet_num_len, 
        _header.GetHeaderType() == PHT_SHORT_HEADER)) {
        LOG_ERROR("decrypt header failed.");
        return false;
    }
    _header.SetPacketNumberLength(packet_num_len);
    cur_pos += _packet_num_offset;
    cur_pos = PacketNumber::Decode(cur_pos, packet_num_len, _packet_number);

    // decrypt packet
    uint8_t *buf = new uint8_t[1450];
    auto payload = BufferSpan(cur_pos, cur_pos + _length - packet_num_len);
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

void HandshakePacket::SetPayload(BufferSpan payload) {
    _payload = payload;
}

}