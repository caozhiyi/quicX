#ifndef HTTP3_STREAM_STREAM_INTERFACE
#define HTTP3_STREAM_STREAM_INTERFACE

namespace quicx {
namespace http3 {

class IStream {
public:
    IStream() {}
    virtual ~IStream() {}
};

}
}

#endif
