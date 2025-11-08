#ifndef HTTP3_STREAM_IF_SEND_STREAM
#define HTTP3_STREAM_IF_SEND_STREAM

#include <memory>
#include <functional>
#include "http3/stream/if_stream.h"
#include "quic/include/if_quic_send_stream.h"

namespace quicx {
namespace http3 {

/**
 * @brief ISendStream is the base class for all HTTP/3 send streams
 * 
 * All HTTP/3 send streams inherit from this class.
 * 
 * The send stream is used to send data to the peer.
 * It is responsible for sending the stream type and ensuring that the stream type is sent before any frames.
 */
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
