
#include "common/log/log.h"
#include "quic/packet/type.h"
#include "common/decode/decode.h"
#include "quic/frame/frame_decode.h"
#include "quic/packet/packet_number.h"
#include "quic/packet/hand_shake_packet.h"
#include "common/buffer/buffer_read_view.h"

namespace quicx {

HandShakePacket::HandShakePacket() {
    _header.GetLongHeaderFlag().SetPacketType(PT_HANDSHAKE);
}

HandShakePacket::HandShakePacket(uint8_t flag):
    _header(flag) {

}

HandShakePacket::~HandShakePacket() {

}

bool HandShakePacket::Encode(std::shared_ptr<IBufferWrite> buffer) {
    if (!_header.EncodeHeader(buffer)) {
        LOG_ERROR("encode header failed");
        return false;
    }

    auto span = buffer->GetWriteSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;
    uint8_t* end = span.GetEnd();
    
    // encode length
    _length = _palyload.GetLength() + _header.GetPacketNumberLength();
    cur_pos = EncodeVarint(cur_pos, _length);

    // encode packet number
    _packet_num_offset = cur_pos - start_pos;
    PacketNumber::Encode(cur_pos, _header.GetPacketNumberLength(), _packet_number); // todo check safe

    // encode payload
    _payload_offset = cur_pos - start_pos;
    memcpy(cur_pos, _palyload.GetStart(), _palyload.GetLength());
    cur_pos += _palyload.GetLength();

    _packet_src_data = std::move(BufferSpan(start_pos, cur_pos));
    buffer->MoveWritePt(cur_pos - span.GetStart());
    return true;
}

bool HandShakePacket::DecodeBeforeDecrypt(std::shared_ptr<IBufferRead> buffer) {
    if (!_header.DecodeHeader(buffer)) {
        LOG_ERROR("decode header failed");
        return false;
    }

    auto span = buffer->GetReadSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;
    uint8_t* end = span.GetEnd();

    // decode length
    cur_pos = DecodeVarint(cur_pos, end, _length);

    // decode packet
    _packet_num_offset = cur_pos - start_pos;

    // decode payload
    cur_pos += _length;
    
    _packet_src_data = std::move(BufferSpan(start_pos, cur_pos));

    buffer->MoveReadPt(cur_pos - span.GetStart());
    return true;
}

bool HandShakePacket::DecodeAfterDecrypt(std::shared_ptr<IBufferRead> buffer) {
    buffer->MoveReadPt(_payload_offset);
    _palyload =  std::move(BufferSpan(buffer->GetData(), _length - _header.GetPacketNumberLength()));
    // decode payload frames
    std::shared_ptr<BufferReadView> view = std::make_shared<BufferReadView>(_palyload.GetStart(), _palyload.GetEnd());
    if(!DecodeFrames(view, _frame_list)) {
        LOG_ERROR("decode frame failed.");
        return false;
    }

    return true;
}

void HandShakePacket::SetPayload(BufferSpan payload) {
    _palyload = payload;
}

}