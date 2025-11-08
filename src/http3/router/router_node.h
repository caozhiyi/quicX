#ifndef HTTP3_ROUTER_ROUTER_NODE
#define HTTP3_ROUTER_ROUTER_NODE

#include <memory>
#include <unordered_map>
#include "http3/router/if_router.h"
#include "http3/router/if_router_node.h"

namespace quicx {
namespace http3 {

class RouterNode:
    public IRouterNode {
public:
    /**
     * @brief Constructor for RouterNode
     * @param type Node type
     * @param section Path section (e.g., "/user", ":id")
     * @param full_path Full path up to this node
     * @param config Route configuration (handler + mode)
     */
    RouterNode(RouterNodeType type, const std::string& section,
        const std::string& full_path, const RouteConfig& config);
    
    virtual ~RouterNode() {}

    const std::string& GetFullPath() { return full_path_; }
    virtual RouterNodeType GetNodeType() { return type_; }

    // Add route with configuration
    virtual bool AddRoute(const std::string& path, int path_offset, const RouteConfig& config);

    virtual bool Match(const std::string& path, int path_offset, const std::string& section, MatchResult& result);

    /**
     * @brief Factory method to create appropriate RouterNode subclass
     * @param path Full URL path
     * @param path_offset Current offset in path
     * @param section Current path section
     * @param config Route configuration
     * @return Shared pointer to created node
     */
    static std::shared_ptr<IRouterNode> MakeNode(const std::string& path, int path_offset,
        const std::string& section, const RouteConfig& config);

protected:
    RouterNodeType type_;
    std::string section_;   // section of the path, like "/user", "/blog"
    std::string full_path_; // full path, like "/user/:info", "/blog/list"

    RouteConfig config_;    // Route configuration (handler + mode)

    std::shared_ptr<IRouterNode> wildcard_node_; // wildcard node
    std::unordered_map<std::string, std::shared_ptr<IRouterNode>> dynamic_param_map_; // dynamic param => router node
    std::unordered_map<std::string, std::shared_ptr<IRouterNode>> static_path_map_;   // static section => router node
};

}
}

#endif
