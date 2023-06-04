#include <cstring>
#include "common/log/log.h"
#include "quic/packet/type.h"
#include "quic/frame/frame_decode.h"
#include "quic/packet/rtt_1_packet.h"
#include "common/buffer/buffer_read_view.h"

namespace quicx {

Rtt1Packet::Rtt1Packet() {

}

Rtt1Packet::Rtt1Packet(uint8_t flag):
    _header(flag) {

}

Rtt1Packet::~Rtt1Packet() {

}

bool Rtt1Packet::Encode(std::shared_ptr<IBufferWrite> buffer) {
    if (!_header.EncodeHeader(buffer)) {
        LOG_ERROR("encode header failed");
        return false;
    }

    auto span = buffer->GetWriteSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;
    uint8_t* end = span.GetEnd();

    _packet_num_offset = cur_pos - start_pos;

    // todo process packet number
    //cur_pos += _header.GetPacketNumberLength();
    _payload_offset = cur_pos - start_pos;

    memcpy(cur_pos, _palyload.GetStart(), _palyload.GetLength());
    cur_pos += _palyload.GetLength();

    _packet_src_data = std::move(BufferSpan(start_pos, cur_pos));
    buffer->MoveWritePt(cur_pos - span.GetStart());
    return true;
}

bool Rtt1Packet::DecodeBeforeDecrypt(std::shared_ptr<IBufferRead> buffer) {
    if (!_header.DecodeHeader(buffer)) {
        LOG_ERROR("decode header failed");
        return false;
    }


    auto span = buffer->GetReadSpan();
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;
    uint8_t* end = span.GetEnd();

    _packet_num_offset = cur_pos - start_pos;

    // todo process packet number
    //cur_pos += _header.GetPacketNumberLength();
    _payload_offset = cur_pos - start_pos;
    cur_pos += span.GetEnd() - cur_pos;
    
    _packet_src_data = std::move(BufferSpan(start_pos, cur_pos));

    buffer->MoveReadPt(cur_pos - span.GetStart());
    return true;
}

bool Rtt1Packet::DecodeAfterDecrypt(std::shared_ptr<IBufferRead> buffer) {
    buffer->MoveReadPt(_payload_offset);
    _palyload =  std::move(BufferSpan(buffer->GetData(), buffer->GetDataLength()));
    // decode payload frames
    std::shared_ptr<BufferReadView> view = std::make_shared<BufferReadView>(_palyload.GetStart(), _palyload.GetEnd());
    if(!DecodeFrames(view, _frame_list)) {
        LOG_ERROR("decode frame failed.");
        return false;
    }

    return true;
}

uint32_t Rtt1Packet::EncodeSize() {
    return 0;
}

bool Rtt1Packet::AddFrame(std::shared_ptr<IFrame> frame) {
    return true;
}

void Rtt1Packet::SetPayload(BufferSpan payload) {
    _palyload = payload;
}

}
