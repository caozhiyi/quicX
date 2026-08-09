#ifndef HTTP3_STREAM_IF_SEND_STREAM
#define HTTP3_STREAM_IF_SEND_STREAM

#include <functional>
#include <memory>


#include "quicx/quic/if_quic_send_stream.h"
#include "http3/stream/if_stream.h"
#include "common/buffer/if_buffer.h"

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
class ISendStream: public IStream {
public:
    ISendStream(StreamType stream_type, const std::shared_ptr<IQuicSendStream>& stream,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler):
        IStream(stream_type, error_handler),
        wrote_type_(false),
        stream_(stream) {}
    virtual ~ISendStream() {}

    virtual uint64_t GetStreamID() override { return stream_->GetStreamID(); }

protected:
    // Ensure stream type is sent before any frames
    bool EnsureStreamPreamble();

    // Encode a control frame into a fresh single-block buffer and append it to
    // the stream's send buffer, then flush.
    //
    // Encoding control frames directly into the stream's own MultiBlockBuffer
    // send buffer fails once its last chunk is full: SETTINGS is written there
    // at init, after which the chunk's writable span is exhausted, so the
    // frame's GetFreeLength() pre-check sees 0 free bytes and the fixed-span
    // BufferEncodeWrapper has nowhere to write (e.g. a server GOAWAY sent
    // during graceful shutdown was silently dropped, stalling HTTP/3 close).
    // A dedicated single-block buffer always has a valid writable span, so the
    // frame encodes cleanly; we then append it as a new chunk and flush, which
    // is exactly the transmit path SendSettings already uses successfully.
    bool EncodeAndAppendControlFrame(
        const std::function<bool(std::shared_ptr<common::IBuffer>)>& encode);

protected:
    bool wrote_type_;  // Track whether stream type has been sent
    std::shared_ptr<IQuicSendStream> stream_;
};

}  // namespace http3
}  // namespace quicx

#endif
