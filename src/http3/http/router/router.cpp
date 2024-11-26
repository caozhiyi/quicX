#include "http3/http/router/router.h"

namespace quicx {
namespace http3 {


RouterErrorCode Router::AddRoute(MothedType mothed, const std::string& path, const http_handler& handler) {
    std::shared_ptr<IRouterNode> cur_node;
    
    auto iter = router_map_.find(mothed);
    if (iter != router_map_.end()) {
        cur_node = iter->second;

    } else {
        cur_node = MakeNode(path_offset == path.size() - 1, section);
        if (!cur_node) {
            return RouterErrorCode::REC_INVAILID_PATH;
        }
        router_map_[section] = cur_node;
    }
    
    if (path_offset >= path.size() - 1) {
        return RouterErrorCode::REC_SUCCESS;
    }
    return cur_node->AddRoute(path, path_offset, handler);
}

MatchResult Router::Match(MothedType mothed, const std::string& path) {
    return MatchResult();
}

}
}
