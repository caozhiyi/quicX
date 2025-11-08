#ifndef HTTP3_ROUTER_ROUTER
#define HTTP3_ROUTER_ROUTER

#include <string>
#include <memory>
#include <unordered_map>
#include "http3/router/if_router.h"
#include "http3/router/if_router_node.h"

namespace quicx {
namespace http3 {

class Router:
    public IRouter {
public:
    Router() {}
    virtual ~Router() {}

    // Add route with configuration
    virtual bool AddRoute(HttpMethod method, const std::string& path, 
                         const RouteConfig& config);

    virtual MatchResult Match(HttpMethod method, const std::string& path);

private:
    std::unordered_map<HttpMethod, std::shared_ptr<IRouterNode>> router_map_; // mothed type => router node
};


}
}

#endif
