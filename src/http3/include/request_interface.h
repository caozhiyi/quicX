#ifndef HTTP3_HTTP_REQUEST_INTERFACE
#define HTTP3_HTTP_REQUEST_INTERFACE

namespace quicx {
namespace http3 {

class IRequest {
public:
    IRequest() {}
    virtual ~IRequest() {}
};

}
}

#endif
