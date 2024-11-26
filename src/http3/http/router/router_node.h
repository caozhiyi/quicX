#ifndef HTTP3_HTTP_ROUTER_ROUTER_NODE
#define HTTP3_HTTP_ROUTER_ROUTER_NODE

#include <memory>
#include <unordered_map>
#include "http3/http/router/type.h"
#include "http3/http/router/util.h"
#include "http3/http/router/if_router_node.h"

namespace quicx {
namespace http3 {

/*
* indicates a section of the path
*/
class RouterNode:
    public IRouterNode {
public:
    RouterNodeParam() {}
    virtual ~RouterNodeParam() {}

    virtual RouterErrorCode AddRoute(const std::string& path, int path_offset, const http_handler& handler);

private:
    std::shared_ptr<IRouterNode> dynamic_param_node_;                          // dynamic param node
    std::unordered_map<std::string, std::shared_ptr<IRouterNode>> router_map_; // path section => router node
};

}
}

#endif
