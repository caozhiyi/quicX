#ifndef HTTP3_ROUTER_ROUTER_NODE
#define HTTP3_ROUTER_ROUTER_NODE

#include <memory>
#include <vector>
#include <unordered_map>
#include "http3/router/util.h"
#include "http3/router/if_router.h"
#include "http3/router/if_router_node.h"

namespace quicx {
namespace http3 {


class RouterNode:
    public IRouterNode {
public:
    RouterNode(RouterNodeType type, const std::string& section,
        const std::string& full_path, const http_handler& handler);
    virtual ~RouterNode() {}

    const std::string& GetFullPath() { return full_path_; }
    virtual RouterNodeType GetNodeType() { return type_; }

    virtual bool AddRoute(const std::string& path, int path_offset, const http_handler& handler);

    virtual bool Match(const std::string& path, int path_offset, const std::string& section, MatchResult& result);

    static std::shared_ptr<IRouterNode> MakeNode(const std::string& path, int path_offset,
        const std::string& section, const http_handler& handler);

protected:
    RouterNodeType type_;
    std::string section_;   // section of the path, like "/user", "/blog"
    std::string full_path_; // full path, like "/user/:info", "/blog/list"

    http_handler handler_; // handler list

    std::shared_ptr<IRouterNode> wildcard_node_; // wildcard node
    std::unordered_map<std::string, std::shared_ptr<IRouterNode>> dynamic_param_map_; // dynamic param => router node
    std::unordered_map<std::string, std::shared_ptr<IRouterNode>> static_path_map_;   // static section => router node
};

}
}

#endif
