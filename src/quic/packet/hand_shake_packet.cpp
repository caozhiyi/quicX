
#include "common/log/log.h"
#include "quic/packet/type.h"
#include "common/decode/decode.h"
#include "quic/frame/frame_decode.h"
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
    
    cur_pos = EncodeVarint(cur_pos, _payload_length);
    _packet_num_offset = cur_pos - start_pos;

    // todo process packet number
    //cur_pos += _header.GetPacketNumberLength();
    _payload_offset = cur_pos - start_pos;

    memcpy(cur_pos, _palyload.GetStart(), _palyload.GetLength());
    cur_pos += _payload_length;

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

    cur_pos = DecodeVarint(cur_pos, end, _payload_length);
    _packet_num_offset = cur_pos - start_pos;

    // todo process packet number
    //cur_pos += _header.GetPacketNumberLength();
    _payload_offset = cur_pos - start_pos;
    cur_pos += _payload_length;
    
    _packet_src_data = std::move(BufferSpan(start_pos, cur_pos));

    buffer->MoveReadPt(cur_pos - span.GetStart());
    return true;
}

bool HandShakePacket::DecodeAfterDecrypt(std::shared_ptr<IBufferRead> buffer) {
    buffer->MoveReadPt(_payload_offset);
    _palyload =  std::move(BufferSpan(buffer->GetData(), _payload_length));
    // decode payload frames
    std::shared_ptr<BufferReadView> view = std::make_shared<BufferReadView>(_palyload.GetStart(), _palyload.GetEnd());
    if(!DecodeFrames(view, _frame_list)) {
        LOG_ERROR("decode frame failed.");
        return false;
    }

    return true;
}

uint32_t HandShakePacket::EncodeSize() {
    return 0;
}

bool HandShakePacket::AddFrame(std::shared_ptr<IFrame> frame) {
    return true;
}

void HandShakePacket::SetPayload(BufferSpan payload) {
    _payload_length = payload.GetLength();
    _palyload = payload;
}

}