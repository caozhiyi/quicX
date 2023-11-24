#ifndef HTTP3_HTTP_ROUTER_ROUTER_NODE
#define HTTP3_HTTP_ROUTER_ROUTER_NODE

#include <string>
#include <unordered_map>
#include "http/router/type.h"

namespace quicx {
namespace http3 {

struct RouterNode {
    std::string _sub_path;
    std::string _full_path;
    http_handler _http_handler;
    std::unordered_map<std::string, RouterNode> _node_map;
};

}
}

#endif
