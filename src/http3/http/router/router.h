#ifndef HTTP3_HTTP_ROUTER_ROUTER
#define HTTP3_HTTP_ROUTER_ROUTER

#include <string>
#include <memory>
#include <unordered_map>
#include "http3/http/router/if_router.h"
#include "http3/http/router/if_router_node.h"

namespace quicx {
namespace http3 {

class Router:
    public IRouter {
public:
    Router() {}
    virtual ~Router() {}

    virtual bool AddRoute(MothedType mothed, const std::string& path, const http_handler& handler);

    virtual MatchResult Match(MothedType mothed, const std::string& path);

private:
    std::unordered_map<MothedType, std::shared_ptr<IRouterNode>> router_map_; // mothed type => router node
};


}
}

#endif