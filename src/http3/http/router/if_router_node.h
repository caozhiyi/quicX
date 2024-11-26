#ifndef HTTP3_HTTP_ROUTER_IF_ROUTER_NODE
#define HTTP3_HTTP_ROUTER_IF_ROUTER_NODE

namespace quicx {
namespace http3 {

enum RouterNodeType {
    RNT_DYNAMIC_PARAM      = 1, // restful param in path
    RNT_STATIC_PATH        = 2, // a static section with handler
    RNT_STATIC_MIDDLE_PATH = 3, // middle section without handler
    RNT_WILDCARD           = 4, // match all section
}

class IRouterNode {
public:
    IRouterNode() {}
    virtual ~IRouterNode() {}

    // add route context
    virtual RouterErrorCode AddRoute(const std::string& path, int path_offset, const http_handler& handler) = 0;

    // router match
    virtual void Match(const std::string& path, int path_offset, MatchResult& result) = 0;
};

}
}

#endif
