#include "http3/router/router_node_root.h"

namespace quicx {
namespace http3 {

RouterNodeRoot::RouterNodeRoot():
    RouterNode(RouterNodeType::RNT_ROOT, "", "", nullptr) {
}

bool RouterNodeRoot::Match(const std::string& path, int path_offset, const std::string& cur_section, MatchResult& result) {
    std::string section = PathParse(path, path_offset);
    if (section.empty()) {
        return false;
    }
    return RouterNode::Match(path, path_offset, section, result);
}

}
}

