#ifndef HTTP3_ROUTER_IF_ROUTER_NODE
#define HTTP3_ROUTER_IF_ROUTER_NODE

#include "http3/router/if_router.h"

namespace quicx {
namespace http3 {

/**
 * @brief Router node type
 * 
 * This type is used to indicate the type of the router node.
 */
enum RouterNodeType {
    RNT_DYNAMIC_PARAM        = 1, // restful param in path with handler
    RNT_DYNAMIC_MIDDLE_PARAM = 2, // restful param in middle path without handler
    RNT_STATIC_PATH          = 3, // a static section with handler
    RNT_STATIC_MIDDLE_PATH   = 4, // middle section without handler
    RNT_WILDCARD             = 5, // match all section
    RNT_ROOT                 = 6, // root node
};

/**
 * @brief Router node
 * 
 * @note
 * This node is used to indicate a section in path.
 * like "/user/:info", "/blog/list", a node indicates "/user", "/blog", "/:info" etc.
 */
class IRouterNode {
public:
    IRouterNode() {}
    virtual ~IRouterNode() = default;

    /**
     * @brief Get the full path
     * 
     * @return The full path
     */
    virtual const std::string& GetFullPath() = 0;

    /**
     * @brief Get the node type
     * 
     * @return The node type
     */
    virtual RouterNodeType GetNodeType() = 0;

    /**
     * @brief Add route with configuration
     * @param path Full URL path
     * @param path_offset Current offset in path
     * @param config Route configuration
     * @return True if route was added successfully, false otherwise
     */
    virtual bool AddRoute(const std::string& path, int path_offset, 
                         const RouteConfig& config) = 0;

    /**
     * @brief Router match
     * 
     * @param path The path to match
     * @param path_offset The current offset in path
     * @param cur_section The current section in path
     * @param result The match result to store the match result, if matched, it will be set to the match result
     * @return True if matched, false otherwise
     */
    virtual bool Match(const std::string& path, int path_offset, const std::string& cur_section, MatchResult& result) = 0;
};

}
}

#endif
