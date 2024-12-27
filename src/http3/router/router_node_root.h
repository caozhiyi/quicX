#ifndef HTTP3_ROUTER_ROUTER_NODE_ROOT
#define HTTP3_ROUTER_ROUTER_NODE_ROOT

#include <string>
#include "http3/router/router_node.h"

namespace quicx {
namespace http3 {

/*
* indicates a root node
*/
class RouterNodeRoot:
    public RouterNode {
public:
    RouterNodeRoot();
    virtual ~RouterNodeRoot() {}

    virtual bool Match(const std::string& path, int path_offset, const std::string& cur_section, MatchResult& result);
};

}
}

#endif
