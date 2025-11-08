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
        wrapper.EncodeVarint(static_cast<uint64_t>(stream_type_));
    }

    if (stream_->Send(buf) <= 0) {
        common::LOG_ERROR("send stream type failed on stream %llu", 
                         stream_->GetStreamID());
        return false;
    }
    
    common::LOG_DEBUG("sent stream type on stream %llu", 
                     stream_->GetStreamID());
    wrote_type_ = true;
    return true;
}

}
}