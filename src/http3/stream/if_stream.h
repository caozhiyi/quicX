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
    // close stream
    virtual void Close(uint32_t error_code) {} // TODO: implement

protected:
    StreamType stream_type_;
    std::function<void(uint64_t stream_id, uint32_t error_code)> error_handler_;
};

}
}

#endif
