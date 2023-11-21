#include "common/log/log.h"
#include "quic/packet/type.h"
#include "common/decode/decode.h"
#include "quic/frame/frame_decode.h"
#include "quic/packet/rtt_0_packet.h"
#include "quic/packet/packet_number.h"
#include "common/buffer/buffer_read_view.h"

namespace quicx {
namespace quic {

Rtt0Packet::Rtt0Packet():
    _length(0),
    _payload_offset(0),
    _packet_num_offset(0) {
    _header.GetLongHeaderFlag().SetPacketType(PT_0RTT);
}

Rtt0Packet::Rtt0Packet(uint8_t flag):
    _length(0),
    _payload_offset(0),
    _packet_num_offset(0),
    _header(flag) {
    _header.GetLongHeaderFlag().SetPacketType(PT_0RTT);
}

Rtt0Packet::~Rtt0Packet() {

}

bool Rtt0Packet::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    if (!_header.EncodeHeader(buffer)) {
        common::LOG_ERROR("encode header failed");
        return false;
    }

    auto span = buffer->GetWriteSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;
    uint8_t* end = span.GetEnd();

    // encode length
    auto len = _payload.GetLength();
    auto len1 = _header.GetPacketNumberLength();
    auto len2 = _crypto_grapher ? _crypto_grapher->GetTagLength() : 0;
    _length = _payload.GetLength() + _header.GetPacketNumberLength() + (_crypto_grapher ? _crypto_grapher->GetTagLength() : 0);
    cur_pos = common::EncodeVarint(cur_pos, _length);

    // encode packet number
    _packet_num_offset = cur_pos - start_pos;
    cur_pos = PacketNumber::Encode(cur_pos, _header.GetPacketNumberLength(), _packet_number);

    // encode payload 
    if (!_crypto_grapher) {
        _payload_offset = cur_pos - start_pos;
        memcpy(cur_pos, _payload.GetStart(), _payload.GetLength());
        cur_pos += _payload.GetLength();
        buffer->MoveWritePt(cur_pos - span.GetStart());
        return true;
    }

     // encode payload whit encrypt
    buffer->MoveWritePt(cur_pos - start_pos);
    auto header_span = _header.GetHeaderSrcData();
    if(!_crypto_grapher->EncryptPacket(_packet_number, header_span, _payload, buffer)) {
        common::LOG_ERROR("encrypt payload failed.");
        return false;
    }

    common::BufferSpan sample = common::BufferSpan(start_pos + _packet_num_offset + 4,
    start_pos + _packet_num_offset + 4 + __header_protect_sample_length);
    if(!_crypto_grapher->EncryptHeader(header_span, sample, header_span.GetLength() + _packet_num_offset, _header.GetPacketNumberLength(),
        _header.GetHeaderType() == PHT_SHORT_HEADER)) {
        common::LOG_ERROR("encrypt header failed.");
        return false;
    }

    return true;
}

bool Rtt0Packet::DecodeWithoutCrypto(std::shared_ptr<common::IBufferRead> buffer) {
    if (!_header.DecodeHeader(buffer)) {
        common::LOG_ERROR("decode header failed");
        return false;
    }

    auto span = buffer->GetReadSpan();
    uint8_t* cur_pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    // decode length
    cur_pos = common::DecodeVarint(cur_pos, end, _length);

    // decode cipher data
    _packet_num_offset = cur_pos - span.GetStart();
    cur_pos += _length;

    // set src data
    _packet_src_data = std::move(common::BufferSpan(span.GetStart(), cur_pos));

    // move buffer read point
    buffer->MoveReadPt(cur_pos - span.GetStart());
    return true;
}

bool Rtt0Packet::DecodeWithCrypto(std::shared_ptr<common::IBuffer> buffer) {
    auto span = _packet_src_data;
    uint8_t* cur_pos = span.GetStart();
    uint8_t* end = span.GetEnd();
    
    if (!_crypto_grapher) {
        // decrypt packet number
        cur_pos += _packet_num_offset;
        cur_pos = PacketNumber::Decode(cur_pos, _header.GetPacketNumberLength(), _packet_number);

        // decode payload frames
        _payload = common::BufferSpan(cur_pos, cur_pos + _length - _header.GetPacketNumberLength());
        std::shared_ptr<common::BufferReadView> view = std::make_shared<common::BufferReadView>(_payload.GetStart(), _payload.GetEnd());
        if(!DecodeFrames(view, _frames_list)) {
            common::LOG_ERROR("decode frame failed.");
            return false;
        }
        return true;
    }
    
    // decrypt header
    uint8_t packet_num_len = 0;
    common::BufferSpan header_span = _header.GetHeaderSrcData();
    common::BufferSpan sample = common::BufferSpan(span.GetStart() + _packet_num_offset + 4,
        span.GetStart() + _packet_num_offset + 4 + __header_protect_sample_length);
    if(!_crypto_grapher->DecryptHeader(header_span, sample, header_span.GetLength() + _packet_num_offset, packet_num_len, 
        _header.GetHeaderType() == PHT_SHORT_HEADER)) {
        common::LOG_ERROR("decrypt header failed.");
        return false;
    }
    _header.SetPacketNumberLength(packet_num_len);
    cur_pos += _packet_num_offset;
    cur_pos = PacketNumber::Decode(cur_pos, packet_num_len, _packet_number);

    // decrypt packet
    auto payload = common::BufferSpan(cur_pos, cur_pos + _length - packet_num_len);
    if(!_crypto_grapher->DecryptPacket(_packet_number, header_span, payload, buffer)) {
        common::LOG_ERROR("decrypt packet failed.");
        return false;
    }

    if(!DecodeFrames(buffer, _frames_list)) {
        common::LOG_ERROR("decode frame failed.");
        return false;
    }

    return true;
}

void Rtt0Packet::SetPayload(common::BufferSpan payload) {
    _payload = payload;
}

}
}