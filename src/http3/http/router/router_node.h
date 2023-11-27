#ifndef HTTP3_HTTP_ROUTER_ROUTER_NODE
#define HTTP3_HTTP_ROUTER_ROUTER_NODE

#include <string>
#include <unordered_map>
#include "http/router/type.h"

namespace quicx {
namespace http3 {

class RouterNode {
public:
    RouterNode();
    ~RouterNode();

    bool AddRoute(const std::string& path, http_handler handler);

    RouterNode* Match(const std::string& uri, std::unordered_map<std::string, std::string>& param);

private:
    bool PathParse(const std::string& path, std::function<bool(const std::string& full_path, const std::string& sub_path)> handle);

private:
    std::string _param;
    std::string _sub_path;
    std::string _full_path;
    http_handler _http_handler;
    std::unordered_map<std::string, RouterNode> _node_map;
};

}
}

#endif
