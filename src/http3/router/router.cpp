#include "http3/router/router.h"
#include "http3/router/router_node_root.h"

namespace quicx {
namespace http3 {

bool Router::AddRoute(HttpMethod method, const std::string& path, const RouteConfig& config) {
    std::shared_ptr<IRouterNode> cur_node;

    auto iter = router_map_.find(method);
    if (iter != router_map_.end()) {
        cur_node = iter->second;

    } else {
        cur_node = std::make_shared<RouterNodeRoot>();
        if (!cur_node) {
            return false;
        }
        router_map_[method] = cur_node;
    }

    return cur_node->AddRoute(path, 0, config);
}

MatchResult Router::Match(HttpMethod method, const std::string& path) {
    MatchResult result;

    auto iter = router_map_.find(method);
    if (iter != router_map_.end()) {
        result.is_match = iter->second->Match(path, 0, "", result);
        if (result.is_match) {
            // Return the local directly: an explicit move here would suppress NRVO.
            return result;
        }
    }

    // P1-H8: fall back to the kAny tree when the method-specific tree has no
    // matching route. Method-specific routes always take precedence. Reset
    // any partial match state (params captured by failed subtree branches)
    // before retrying on the fallback tree.
    if (method != HttpMethod::kAny) {
        auto any_iter = router_map_.find(HttpMethod::kAny);
        if (any_iter != router_map_.end()) {
            result.params.clear();
            result.config = RouteConfig();
            result.is_match = any_iter->second->Match(path, 0, "", result);
        }
    }
    return result;
}

}  // namespace http3
}  // namespace quicx
