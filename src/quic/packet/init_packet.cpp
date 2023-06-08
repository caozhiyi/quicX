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

InitPacket::InitPacket():
    _payload_offset(0),
    _packet_num_offset(0),
    _token_length(0) {
    _header.GetLongHeaderFlag().SetPacketType(PT_INITIAL);
}

InitPacket::InitPacket(uint8_t flag):
    _header(flag),
    _payload_offset(0),
    _packet_num_offset(0),
    _token_length(0) {

}

InitPacket::~InitPacket() {

}

bool InitPacket::Encode(std::shared_ptr<IBufferWrite> buffer, std::shared_ptr<ICryptographer> crypto_grapher) {
    if (!_header.EncodeHeader(buffer)) {
        LOG_ERROR("encode header failed");
        return false;
    }

    auto span = buffer->GetWriteSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;
    uint8_t* end = span.GetEnd();

    // encode token
    cur_pos = EncodeVarint(cur_pos, _token_length);
    if (_token_length > 0) {
        memcpy(cur_pos, _token, _token_length);
        cur_pos += _token_length;
    }

    // encode length
    auto len = _payload.GetLength();
    auto len1 = _header.GetPacketNumberLength();
    auto len2 = crypto_grapher ? crypto_grapher->GetTagLength() : 0;
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

bool InitPacket::Decode(std::shared_ptr<IBufferRead> buffer) {
    if (!_header.DecodeHeader(buffer)) {
        LOG_ERROR("decode header failed");
        return false;
    }

    auto span = buffer->GetReadSpan();
    uint8_t* cur_pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    // decode token
    cur_pos = DecodeVarint(cur_pos, end, _token_length);
    _token = cur_pos;
    cur_pos += _token_length;
    if (_token_length > 0) {
        LOG_DEBUG("get initial token:%s", _token);
    }
    
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

bool InitPacket::Decode(std::shared_ptr<IBuffer> buffer, std::shared_ptr<ICryptographer> crypto_grapher) {
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
    auto payload = BufferSpan(cur_pos, cur_pos + _length - packet_num_len);
    if(!crypto_grapher->DecryptPacket(_packet_number, header_span, payload, buffer)) {
        LOG_ERROR("decrypt packet failed.");
        return false;
    }

    if(!DecodeFrames(buffer, _frame_list)) {
        LOG_ERROR("decode frame failed.");
        return false;
    }

    return true;
}

void InitPacket::SetToken(uint8_t* token, uint32_t len) {
    _token = token;
    _token_length = len;
}

void InitPacket::SetPayload(BufferSpan payload) {
    _payload = payload;
}

}