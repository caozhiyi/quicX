#include "http3/http/router/router.h"
#include "http3/http/router/router_node_root.h"

namespace quicx {
namespace http3 {

bool Router::AddRoute(MothedType mothed, const std::string& path, const http_handler& handler) {
    std::shared_ptr<IRouterNode> cur_node;
    
    auto iter = router_map_.find(mothed);
    if (iter != router_map_.end()) {
        cur_node = iter->second;

    } else {
        cur_node = std::make_shared<RouterNodeRoot>();
        if (!cur_node) {
            return false;
        }
        router_map_[mothed] = cur_node;
    }

    return cur_node->AddRoute(path, 0, handler);
}

MatchResult Router::Match(MothedType mothed, const std::string& path) {
    MatchResult result;
    
    auto iter = router_map_.find(mothed);
    if (iter == router_map_.end()) {
        return std::move(result);
    }

    result.is_match = iter->second->Match(path, 0, "", result);
    return std::move(result);
}

}
}
