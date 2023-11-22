#ifndef HTTP3_HTTP_RESPONSE_INTERFACE
#define HTTP3_HTTP_RESPONSE_INTERFACE

namespace quicx {
namespace http3 {

class IResponse {
public:
    IResponse() {}
    virtual ~IResponse() {}
};

}
}

#endif
