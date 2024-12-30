#ifndef HTTP3_STREAM_IF_STREAM
#define HTTP3_STREAM_IF_STREAM

#include "http3/stream/type.h"

namespace quicx {
namespace http3 {

class IStream {
public:
    IStream() {}
    virtual ~IStream() {}
    virtual StreamType GetType() = 0;
};

}
}

#endif
