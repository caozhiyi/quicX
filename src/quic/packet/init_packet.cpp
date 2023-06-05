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

bool InitPacket::Encode(std::shared_ptr<IBufferWrite> buffer) {
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
    _length = _palyload.GetLength() + _header.GetPacketNumberLength() + (_crypto_grapher ? _crypto_grapher->GetTagLength() : 0);
    cur_pos = EncodeVarint(cur_pos, _length);

    // encode packet number
    _packet_num_offset = cur_pos - start_pos;
    cur_pos = PacketNumber::Encode(cur_pos, _header.GetPacketNumberLength(), _packet_number);

    // encode payload 
    if (!_crypto_grapher) {
        _payload_offset = cur_pos - start_pos;
        memcpy(cur_pos, _palyload.GetStart(), _palyload.GetLength());
        cur_pos += _palyload.GetLength();
        buffer->MoveWritePt(cur_pos - span.GetStart());
        return true;
    }

     // encode payload whit encrypt
    buffer->MoveWritePt(cur_pos - start_pos);
    auto header_span = _header.GetHeaderSrcData();
    if(!_crypto_grapher->EncryptPacket(_packet_number, header_span, _palyload, buffer)) {
        LOG_ERROR("encrypt payload failed.");
        return false;
    }

    BufferSpan sample = BufferSpan(start_pos + _packet_num_offset + 4,
    start_pos + _packet_num_offset + 4 + __header_protect_sample_length);
    if(!_crypto_grapher->EncryptHeader(header_span, sample, header_span.GetLength() + _packet_num_offset, _header.GetPacketNumberLength(),
        _header.GetHeaderType() == PHT_SHORT_HEADER)) {
        LOG_ERROR("encrypt header failed.");
        return false;
    }

    return true;
}

bool InitPacket::DecodeBeforeDecrypt(std::shared_ptr<IBufferRead> buffer) {
    if (!_header.DecodeHeader(buffer)) {
        LOG_ERROR("decode header failed");
        return false;
    }

    auto span = buffer->GetReadSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;
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
    _packet_num_offset = cur_pos - start_pos;
    
    if (!_crypto_grapher) {
        // decrypt packet number
        cur_pos = PacketNumber::Decode(cur_pos, _header.GetPacketNumberLength(), _packet_number);

        // decode payload frames
        _palyload =  std::move(BufferSpan(cur_pos, _length - _header.GetPacketNumberLength()));
        std::shared_ptr<BufferReadView> view = std::make_shared<BufferReadView>(_palyload.GetStart(), _palyload.GetEnd());
        if(!DecodeFrames(view, _frame_list)) {
            LOG_ERROR("decode frame failed.");
            return false;
        }
        return true;
    }
    
    // decrypt header
    uint64_t packet_num = 0;
    uint32_t packet_num_len = 0;
    BufferSpan header_span = _header.GetHeaderSrcData();
    BufferSpan sample = BufferSpan(start_pos + _packet_num_offset + 4,
        start_pos + _packet_num_offset + 4 + __header_protect_sample_length);
    if(!_crypto_grapher->DecryptHeader(header_span, sample, header_span.GetLength() + _packet_num_offset, _header.GetHeaderType() == PHT_SHORT_HEADER,
        packet_num, packet_num_len)) {
        LOG_ERROR("decrypt header failed.");
        return false;
    }
    _header.SetPacketNumberLength(packet_num_len);
    cur_pos = PacketNumber::Decode(cur_pos, packet_num_len, _packet_number);

    // decrypt packet
    static const uint32_t __buf_len = 1450;
    uint8_t *buf = new uint8_t[__buf_len];
    std::shared_ptr<IBuffer> out_plaintext = std::make_shared<Buffer>(buf, buf + __buf_len);
    auto payload = BufferSpan(cur_pos, cur_pos + _length - packet_num_len);
    if(!_crypto_grapher->DecryptPacket(_packet_number, header_span, payload, out_plaintext)) {
        LOG_ERROR("decrypt packet failed.");
        return false;
    }
    buffer->MoveReadPt(_length - packet_num_len);
    _palyload = out_plaintext->GetReadSpan();

    if(!DecodeFrames(out_plaintext, _frame_list)) {
        LOG_ERROR("decode frame failed.");
        return false;
    }

    return true;
}

bool InitPacket::DecodeAfterDecrypt(std::shared_ptr<IBufferRead> buffer) {
    return true;
}

void InitPacket::SetToken(uint8_t* token, uint32_t len) {
    _token = token;
    _token_length = len;
}

void InitPacket::SetPayload(BufferSpan payload) {
    _palyload = payload;
}

}