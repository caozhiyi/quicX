#ifndef HTTP3_STREAM_IF_SEND_STREAM
#define HTTP3_STREAM_IF_SEND_STREAM

#include <memory>
#include <functional>
#include <unordered_map>
#include "http3/stream/if_stream.h"
#include "quic/include/if_quic_send_stream.h"

namespace quicx {
namespace http3 {

class ISendStream:
    public IStream {
public:
    ISendStream(StreamType stream_type,
                const std::shared_ptr<quic::IQuicSendStream>& stream,
                const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler):
                IStream(stream_type, error_handler),
                wrote_type_(false),
                stream_(stream) {}
    virtual ~ISendStream() {}

    virtual uint64_t GetStreamID() override { return stream_->GetStreamID(); }
protected:
    // Ensure stream type is sent before any frames
    bool EnsureStreamPreamble();

protected:
    bool wrote_type_;  // Track whether stream type has been sent
    std::shared_ptr<quic::IQuicSendStream> stream_;
};

}
}

#endif
