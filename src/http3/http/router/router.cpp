#include <string.h> 
#include "http/router/router.h"

namespace quicx {
namespace http3 {

const MothedType Router::__mothed_types[] = {MT_GET, MT_HEAD, MT_POST, MT_PUT, MT_DELETE, MT_CONNECT, MT_OPTIONS, MT_TRACE, MT_PATCH};

Router::Router() {
    _roots = new RouterNode[sizeof(__mothed_types)];
    memset(_roots, 0, sizeof(__mothed_types));
}

Router::~Router() {
    delete[] _roots;
}

bool Router::AddRoute(uint32_t mothed, const std::string& path, http_handler handler) {
    if (path.empty()) {
        return false;
    }

    for (size_t i = 0; i < sizeof(__mothed_types); i++) {
        if (mothed & __mothed_types[i]) {
            if (!_roots[__mothed_types[i]].AddRoute(path, handler)) {
                return false;
            }
        }
    }
    return true;
}

RouterNode* Router::Match(uint32_t mothed, const std::string& uri, std::unordered_map<std::string, std::string>& param) {
    if (uri.empty() || mothed > MT_PATCH) {
        return nullptr;
    }

    return  _roots[__mothed_types[mothed]].Match(uri, param);
}

}
}