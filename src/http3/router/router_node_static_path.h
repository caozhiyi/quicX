#ifndef HTTP3_ROUTER_ROUTER_NODE_STATIC_PATH
#define HTTP3_ROUTER_ROUTER_NODE_STATIC_PATH

#include <string>
#include "http3/router/router_node.h"

namespace quicx {
namespace http3 {

/*
* indicates a static path node
*/
class RouterNodeStaticPath:
    public RouterNode {
public:
    RouterNodeStaticPath(RouterNodeType type, const std::string& section,
        const std::string& full_path, const RouteConfig& config);
    
    virtual ~RouterNodeStaticPath() {}

    // router match
    virtual bool Match(const std::string& path, int path_offset, const std::string& cur_section, MatchResult& result);
};

}
}

#endif
