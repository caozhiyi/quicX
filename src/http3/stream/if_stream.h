#ifndef HTTP3_STREAM_IF_STREAM
#define HTTP3_STREAM_IF_STREAM

#include <functional>
#include "http3/stream/type.h"

namespace quicx {
namespace http3 {

class IStream {
public:
    IStream(const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler): error_handler_(error_handler) {}
    virtual ~IStream() {}
    virtual StreamType GetType() = 0;
    virtual uint64_t GetStreamID() = 0;

protected:
    std::function<void(uint64_t stream_id, uint32_t error_code)> error_handler_;
};

}
}

#endif
