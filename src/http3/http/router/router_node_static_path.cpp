#include "http3/http/router/router_node_static_path.h"

namespace quicx {
namespace http3 {


RouterNodeStaticPath::RouterNodeStaticPath(RouterNodeType type, const std::string& section,
    const std::string& full_path, const http_handler& handler):
    RouterNode(type, section, full_path, handler) {

}

bool RouterNodeStaticPath::Match(const std::string& path, int path_offset, const std::string& cur_section, MatchResult& result) {
    // check match done
    if (path_offset >= path.length()) {
        // match done, current node is the last node
        if (type_ == RouterNodeType::RNT_STATIC_PATH) {
            result.handler = handler_;
            return true;
        }
        
        result.handler = nullptr;
        return false;
    }
    
    std::string section = PathParse(path, path_offset);
    if (section.empty()) {
        return false;
    }

    return RouterNode::Match(path, path_offset, section, result);
}

}
}
