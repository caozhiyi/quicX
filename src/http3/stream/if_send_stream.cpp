#include "common/log/log.h"
#include "http3/stream/if_send_stream.h"
#include "common/buffer/buffer_encode_wrapper.h"

namespace quicx {
namespace http3 {

bool ISendStream::EnsureStreamPreamble() {
    if (wrote_type_) {
        return true;
    }
    
    // Send stream type for Control Stream (RFC 9114 Section 6.2.1)
    auto buffer = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    common::BufferEncodeWrapper wrapper(buffer);
    wrapper.EncodeVarint(static_cast<uint64_t>(stream_type_));

    common::LOG_DEBUG("sent stream type on stream %llu", 
                     stream_->GetStreamID());
    wrote_type_ = true;
    return true;
}

}
}