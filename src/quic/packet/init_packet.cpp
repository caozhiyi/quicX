#include "common/log/log.h"
#include "quic/common/version.h"
#include "common/decode/decode.h"
#include "quic/common/constants.h"
#include "quic/packet/init_packet.h"
#include "quic/frame/frame_decode.h"
#include "common/buffer/buffer_read_view.h"

namespace quicx {

InitPacket::InitPacket():
    _packet_num_offset(0) {
    _header.GetLongHeaderFlag().SetPacketType(PT_INITIAL);
}

InitPacket::InitPacket(uint8_t flag):
    _header(flag),
    _packet_num_offset(0) {

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

    cur_pos = EncodeVarint(cur_pos, _token_length);
    if (_token_length > 0) {
        memcpy(cur_pos, _token, _token_length);
        cur_pos += _token_length;
    }
    
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

bool InitPacket::DecodeBeforeDecrypt(std::shared_ptr<IBufferRead> buffer) {
    if (!_header.DecodeHeader(buffer)) {
        LOG_ERROR("decode header failed");
        return false;
    }

    // check version
    /*if (!CheckVersion(_header.GetVersion())) {
        return false;
    }*/

    auto span = buffer->GetReadSpan();

    // check buffer length
    /*if (span.GetLength() <= __min_initial_size) {
        LOG_ERROR("buffer is too small for initial packet");
        return false;
    }*/
    
    uint8_t* start_pos = span.GetStart();
    uint8_t* cur_pos = start_pos;
    uint8_t* end = span.GetEnd();
    cur_pos = DecodeVarint(cur_pos, end, _token_length);
    _token = cur_pos;
    cur_pos += _token_length;
    LOG_DEBUG("get initial token:%s", _token);

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

bool InitPacket::DecodeAfterDecrypt(std::shared_ptr<IBufferRead> buffer) {
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

uint32_t InitPacket::EncodeSize() {
    return 0;
}

bool InitPacket::AddFrame(std::shared_ptr<IFrame> frame) {
    return true;
}

void InitPacket::SetToken(uint8_t* token, uint32_t len) {
    _token = token;
    _token_length = len;
}

void InitPacket::SetPayload(BufferSpan payload) {
    _payload_length = payload.GetLength();
    _palyload = payload;
}

}