#ifndef HTTP3_ROUTER_IF_ROUTER_NODE
#define HTTP3_ROUTER_IF_ROUTER_NODE

#include "http3/router/if_router.h"

namespace quicx {
namespace http3 {

enum RouterNodeType {
    RNT_DYNAMIC_PARAM        = 1, // restful param in path with handler
    RNT_DYNAMIC_MIDDLE_PARAM = 2, // restful param in middle path without handler
    RNT_STATIC_PATH          = 3, // a static section with handler
    RNT_STATIC_MIDDLE_PATH   = 4, // middle section without handler
    RNT_WILDCARD             = 5, // match all section
    RNT_ROOT                 = 6, // root node
};

/*
* indicates a section in path.
* like "/user/:info", "/blog/list", a node indicates "/user", "/blog", "/:info" etc.
*/
class IRouterNode {
public:
    IRouterNode() {}
    virtual ~IRouterNode() {}

    virtual const std::string& GetFullPath() = 0;
    virtual RouterNodeType GetNodeType() = 0;

    // add route context
    virtual bool AddRoute(const std::string& path, int path_offset, const http_handler& handler) = 0;

    // router match, return true if matched, false otherwise
    // path: the full match path
    // path_offset: the offset of  the full path
    // cur_section: the current section in path
    // result: the match result
    virtual bool Match(const std::string& path, int path_offset, const std::string& cur_section, MatchResult& result) = 0;
};

}
}

#endif
