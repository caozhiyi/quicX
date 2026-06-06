#ifndef HTTP3_STREAM_IF_STREAM
#define HTTP3_STREAM_IF_STREAM

#include <functional>
#include "http3/stream/type.h"

namespace quicx {
namespace http3 {

/**
 * @brief IStream is the base class for all HTTP/3 streams
 * 
 * All HTTP/3 streams inherit from this class.
 */
class IStream {
public:
    IStream(StreamType stream_type,
            const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler):
            stream_type_(stream_type),
            error_handler_(error_handler) {}
    virtual ~IStream() {}
    // get stream type
    virtual StreamType GetType() { return stream_type_; }
    // get stream id
    virtual uint64_t GetStreamID() = 0;
    // Close the HTTP/3 stream with an application-level error code.
    //
    // Currently a no-op on the base class and not overridden by any
    // derived stream: the only paths that need to terminate a stream are
    // already routed through the underlying QUIC stream's Close() / Reset()
    // (see ReqRespBaseStream::SendBodyDirectly / *::~SenderStream). This
    // virtual is kept as an interface stub for a future redesign in which
    // higher-level code (e.g. HTTP/3 connection shutdown / GOAWAY drain)
    // can ask each stream to wind itself down with a specific HTTP/3
    // error code without having to know the underlying QUIC stream type.
    // Tracked as a learning-only limitation in
    // learning_project_roadmap.md §2.
    virtual void Close(uint32_t error_code) {}

protected:
    StreamType stream_type_;
    std::function<void(uint64_t stream_id, uint32_t error_code)> error_handler_;
};

}
}

#endif
