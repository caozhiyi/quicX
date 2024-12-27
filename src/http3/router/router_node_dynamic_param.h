#ifndef HTTP3_ROUTER_ROUTER_NODE_DYNAMIC_PARAM
#define HTTP3_ROUTER_ROUTER_NODE_DYNAMIC_PARAM

#include <string>
#include "http3/router/router_node.h"

namespace quicx {
namespace http3 {

/*
* indicates a dynamic param node
*/
class RouterNodeDynamicParam:
    public RouterNode {
public:
    RouterNodeDynamicParam(RouterNodeType type, const std::string& section,
        const std::string& full_path, const http_handler& handler);
    virtual ~RouterNodeDynamicParam() {}

    // router match
    virtual bool Match(const std::string& path, int path_offset, const std::string& cur_section, MatchResult& result);

private:
    std::string param_name_;
};

}
}

#endif
