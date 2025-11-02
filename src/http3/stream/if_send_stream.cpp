#include "common/log/log.h"
#include "common/buffer/buffer.h"
#include "http3/stream/if_send_stream.h"
#include "common/buffer/buffer_encode_wrapper.h"

namespace quicx {
namespace http3 {

bool ISendStream::EnsureStreamPreamble() {
    if (wrote_type_) {
        return true;
    }
    
    // Send stream type for Control Stream (RFC 9114 Section 6.2.1)
    uint8_t tmp[8] = {0};
    auto buf = std::make_shared<common::Buffer>(tmp, sizeof(tmp));
    
    {
        common::BufferEncodeWrapper wrapper(buf);
        wrapper.EncodeVarint(static_cast<uint64_t>(StreamType::kControl));
        // Wrapper will flush on destruction
    }
    
    if (stream_->Send(buf) <= 0) {
        common::LOG_ERROR("ControlSenderStream: failed to send stream type on stream %llu", 
                         stream_->GetStreamID());
        return false;
    }
    
    common::LOG_DEBUG("ControlSenderStream: sent stream type kControl (0x00) on stream %llu", 
                     stream_->GetStreamID());
    wrote_type_ = true;
    return true;
}

}
}