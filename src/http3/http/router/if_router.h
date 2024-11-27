#ifndef HTTP3_HTTP_ROUTER_IF_ROUTER
#define HTTP3_HTTP_ROUTER_IF_ROUTER

#include <string>
#include <unordered_map>
#include "http3/include/type.h"
#include "http3/http/router/type.h"

namespace quicx {
namespace http3 {

struct MatchResult {
    RouterErrorCode error;                               // error code of router operation 
    http_handler handler;                                // http handler
    std::unordered_map<std::string, std::string> params; // request params
};

class IRouter {
public:
    IRouter() {}
    virtual ~IRouter() {}

    // add route context
    virtual bool AddRoute(MothedType mothed, const std::string& path, const http_handler& handler) = 0;

    // router match
    virtual MatchResult Match(MothedType mothed, const std::string& path) = 0;
};


}
}

#endif
