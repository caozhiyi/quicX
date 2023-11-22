#ifndef HTTP3_HTTP_ROUTER_TYPE
#define HTTP3_HTTP_ROUTER_TYPE

#include <functional>

namespace quicx {
namespace http3 {

class IRequest;
class IResponse;

typedef std::function<void(const IRequest& request, IResponse& response)> http_handler;

}
}

#endif
