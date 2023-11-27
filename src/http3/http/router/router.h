#ifndef HTTP3_HTTP_ROUTER_ROUTER
#define HTTP3_HTTP_ROUTER_ROUTER

#include <functional>
#include <unordered_map>
#include "http/router/type.h"
#include "http/router/router_node.h"

namespace quicx {
namespace http3 {

class Router {
public:
    Router();
    virtual ~Router();

    bool AddRoute(uint32_t mothed, const std::string& path, http_handler handler);

    RouterNode* Match(uint32_t mothed, const std::string& uri, std::unordered_map<std::string, std::string>& param);

private:
    static const MothedType __mothed_types[];
    RouterNode *_roots;
};

 

}
}

#endif
