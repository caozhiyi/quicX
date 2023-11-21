#ifndef HTTP3_HTTP_HEADER_INTERFACE
#define HTTP3_HTTP_HEADER_INTERFACE

namespace quicx {
namespace http3 {

class IHeader {
public:
    IHeader() {}
    virtual ~IHeader() {}
};

}
}

#endif
