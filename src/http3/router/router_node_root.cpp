#include "http3/router/router_node_root.h"

namespace quicx {
namespace http3 {

RouterNodeRoot::RouterNodeRoot():
    RouterNode(RouterNodeType::RNT_ROOT, "", "", nullptr) {
}

bool RouterNodeRoot::Match(const std::string& path, int path_offset, const std::string& cur_section, MatchResult& result) {
    // check match done
    if (path_offset >= path.length()) {
        // match done, current node is the last node
        if (type_ == RouterNodeType::RNT_STATIC_PATH) {
            result.handler = handler_;
            result.is_match = true;
            return true;
        }
        
        result.handler = nullptr;
        result.is_match = false;
        return false;
    }
    
    std::string section = PathParse(path, path_offset);
    if (section.empty()) {
        result.is_match = false;
        return false;
    }

    return RouterNode::Match(path, path_offset, section, result);
}

}
}

