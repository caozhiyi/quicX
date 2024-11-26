#ifndef HTTP3_HTTP_IF_HEADER
#define HTTP3_HTTP_IF_HEADER

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
