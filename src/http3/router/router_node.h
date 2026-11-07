#ifndef HTTP3_ROUTER_ROUTER_NODE
#define HTTP3_ROUTER_ROUTER_NODE

#include <map>
#include <memory>
#include <unordered_map>

#include "http3/router/if_router.h"
#include "http3/router/if_router_node.h"

namespace quicx {
namespace http3 {

class RouterNode: public IRouterNode {
public:
    /**
     * @brief Constructor for RouterNode
     * @param type Node type
     * @param section Path section (e.g., "/user", ":id")
     * @param full_path Full path up to this node
     * @param config Route configuration (handler + mode)
     */
    RouterNode(
        RouterNodeType type, const std::string& section, const std::string& full_path, const RouteConfig& config);

    virtual ~RouterNode() {}

    const std::string& GetFullPath() { return full_path_; }
    virtual RouterNodeType GetNodeType() { return type_; }

    /**
     * @brief Attach a route handler to this node
     *
     * P1-H8: handler presence is decoupled from the node type so a node can
     * carry BOTH a handler and children (register "/a/b" then "/a": the
     * second registration upgrades the "/a" middle node with a handler).
     *
     * @param config Route configuration (handler + mode)
     * @return True if attached; false if this node already has a handler
     *         (duplicate route registration — the existing one is kept).
     */
    virtual bool SetRouteHandler(const RouteConfig& config) {
        if (has_handler_) {
            return false;  // duplicate route: keep the first registration
        }
        config_ = config;
        has_handler_ = true;
        return true;
    }

    // Add route with configuration
    virtual bool AddRoute(const std::string& path, int path_offset, const RouteConfig& config);

    virtual bool Match(const std::string& path, int path_offset, const std::string& section, MatchResult& result);

    /**
     * @brief Whether this node terminates a registered route (has a handler)
     *
     * Replaces the old "node type decides handler presence" rule: middle
     * nodes upgraded via SetRouteHandler also report true.
     */
    virtual bool HasRouteHandler() const { return has_handler_; }

    /**
     * @brief Factory method to create appropriate RouterNode subclass
     * @param path Full URL path
     * @param path_offset Current offset in path
     * @param section Current path section
     * @param config Route configuration
     * @return Shared pointer to created node
     */
    static std::shared_ptr<IRouterNode> MakeNode(
        const std::string& path, int path_offset, const std::string& section, const RouteConfig& config);

protected:
    RouterNodeType type_;
    std::string section_;    // section of the path, like "/user", "/blog"
    std::string full_path_;  // full path, like "/user/:info", "/blog/list"

    RouteConfig config_;  // Route configuration (handler + mode)

    // P1-H8: whether this node terminates a registered route. Initialized by
    // the constructor from the leaf-ness encoded in type_ (leaf types are
    // RNT_STATIC_PATH / RNT_DYNAMIC_PARAM / RNT_WILDCARD); a middle node can
    // later be upgraded via SetRouteHandler when a shorter route is
    // registered under an existing prefix.
    bool has_handler_ = false;

    std::shared_ptr<IRouterNode> wildcard_node_;                                       // wildcard node
    // #7 (code review round 2): ordered map so Match()'s dynamic-param
    // iteration is deterministic. With unordered_map, two same-level dynamic
    // nodes that both fully match a path ("/users/:id" vs "/users/:name",
    // both leaves, for "/users/42") won in hash order — an unspecified
    // parameter binding. Lexicographic section order makes the winner stable.
    // Registering different param names at the same level remains legal:
    // structurally-different routes (e.g. /users/:id plus
    // /users/:user_id/posts/:post_id) are disambiguated by the
    // match-with-backtrack semantics, not by iteration order.
    std::map<std::string, std::shared_ptr<IRouterNode>> dynamic_param_map_;  // dynamic param => router node
    std::unordered_map<std::string, std::shared_ptr<IRouterNode>> static_path_map_;    // static section => router node
};

}  // namespace http3
}  // namespace quicx

#endif  // HTTP3_ROUTER_ROUTER_NODE
