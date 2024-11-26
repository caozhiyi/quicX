#ifndef HTTP3_HTTP_ROUTER_ROUTER_NODE
#define HTTP3_HTTP_ROUTER_ROUTER_NODE

#include <string>
#include "http3/http/router/router_node.h"

namespace quicx {
namespace http3 {

/*
* indicates a 
*/
class RouterNodeParam:
    public RouterNode {
public:
    RouterNodeParam() {}
    virtual ~RouterNodeParam() {}

    // router match
    virtual void Match(const std::string& path, int path_offset, MatchResult& result) = 0;

private:
    std::string param_name_;
};

}
}

#endif
