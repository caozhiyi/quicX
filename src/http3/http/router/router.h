#ifndef HTTP3_HTTP_ROUTER_ROUTER
#define HTTP3_HTTP_ROUTER_ROUTER

#include "http/router/type.h"

namespace quicx {
namespace http3 {

class Router {
public:
    Router() {}
    virtual ~Router() {}

    void AddRoute(const std::string& path, http_handler handler);
};

}
}

#endif
