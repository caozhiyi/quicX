#include "http3/http/router/util.h"
#include "http3/http/router/router_node.h"

namespace quicx {
namespace http3 {


RouterErrorCode RouterNode::AddRoute(const std::string& path, int path_offset, const http_handler& handler) {
    std::string section = PathParse(path, path_offset);
    if (section.empty()) {
        return RouterErrorCode::REC_INVAILID_PATH;
    }

    std::shared_ptr<IRouterNode> cur_node;
    auto iter = router_map_.find(section);
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

std::shared_ptr<IRouterNode> RouterNode::MakeNode(bool is_end, const std::string& section) {
    if (section[0] != '/') {
        return nullptr;
    }
    
    RouterNodeType node_type;
    if (is_end) {
        node_type = RouterNodeType::RNT_HANDLER;

    } else {
        // there is nothing between //
        if (section.size() == 1) {
            return nullptr;
        }
        
        switch (section[1]) {
        case ':':
            node_type = RouterNodeType::RNT_PARAM;
            break;
        case '*':
            node_type = RouterNodeType::RNT_WILDCARD;
            break;

        default:
            node_type = RouterNodeType::RNT_NULL;
            break;
        }
    }
    
    return std::make_shared<RouterNode>(node_type, section);
}

}
}

#endif
