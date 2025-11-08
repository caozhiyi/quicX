#ifndef HTTP3_ROUTER_ROUTER_NODE_WILDCARD
#define HTTP3_ROUTER_ROUTER_NODE_WILDCARD

#include <string>
#include "http3/router/router_node.h"

namespace quicx {
namespace http3 {

/*
* indicates a wildcard node
*/
class RouterNodeWildcard:
    public RouterNode {
public:
    RouterNodeWildcard(const std::string& section,
        const std::string& full_path, const RouteConfig& config);
    
    virtual ~RouterNodeWildcard() {}

    // router match
    virtual bool Match(const std::string& path, int path_offset, const std::string& cur_section, MatchResult& result);
};

}
}

#endif
