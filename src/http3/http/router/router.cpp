#include "http/router/router.h"

namespace quicx {
namespace http3 {

const MothedType Router::__mothed_types[] = {MT_GET, MT_HEAD, MT_POST, MT_PUT, MT_DELETE, MT_CONNECT, MT_OPTIONS, MT_TRACE, MT_PATCH};

Router::Router() {
    _roots = new RouterNode[sizeof(__mothed_types)];
}

Router::~Router() {
    delete[] _roots;
}

bool Router::AddRoute(uint32_t mothed, const std::string& path, http_handler handler) {
    if (path.empty()) {
        return false;
    }

}

RouterNode* Router::Match(const std::string& uri) {
    return nullptr;
}

bool Router::AddRoute(RouterNode& root, const std::string& path, http_handler handler) {
    for (size_t i = 0; i < path.size(); i++){
        /* code */
    }
    
}

}
}