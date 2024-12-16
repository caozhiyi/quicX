#include "common/log/log.h"
#include "common/decode/decode.h"
#include "common/buffer/if_buffer.h"
#include "common/alloter/if_alloter.h"
#include "quic/frame/reset_stream_frame.h"

namespace quicx {
namespace quic {

ResetStreamFrame::ResetStreamFrame(): 
    IStreamFrame(FT_RESET_STREAM),
    app_error_code_(0), 
    final_size_(0) {

}

ResetStreamFrame::~ResetStreamFrame() {

}

bool ResetStreamFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = span.GetStart();
    pos = common::FixedEncodeUint16(pos, frame_type_);
    pos = common::EncodeVarint(pos, stream_id_);
    pos = common::EncodeVarint(pos, app_error_code_);
    pos = common::EncodeVarint(pos, final_size_);

    buffer->MoveWritePt(pos - span.GetStart());
    return true;
}

bool ResetStreamFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = common::FixedDecodeUint16(pos, end, frame_type_);
        if (frame_type_ != FT_RESET_STREAM) {
            return false;
        }
    }
    pos = common::DecodeVarint(pos, end, stream_id_);
    pos = common::DecodeVarint(pos, end, app_error_code_);
    pos = common::DecodeVarint(pos, end, final_size_);

    buffer->MoveReadPt(pos - span.GetStart());
    return true;
}

uint32_t ResetStreamFrame::EncodeSize() {
    return sizeof(ResetStreamFrame);
}

}
}