#include "http3/router/util.h"
#include "http3/router/router_node_static_path.h"

namespace quicx {
namespace http3 {

RouterNodeStaticPath::RouterNodeStaticPath(
    RouterNodeType type, const std::string& section, const std::string& full_path, const RouteConfig& config):
    RouterNode(type, section, full_path, config) {}

bool RouterNodeStaticPath::Match(
    const std::string& path, int path_offset, const std::string& cur_section, MatchResult& result) {
    // check match done
    if (path_offset >= path.length()) {
        // match done, current node is the last node
        if (type_ == RouterNodeType::RNT_STATIC_PATH) {
            result.config = config_;
            result.is_match = true;
            return true;
        }

        result.config = RouteConfig();
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

}  // namespace http3
}  // namespace quicx
