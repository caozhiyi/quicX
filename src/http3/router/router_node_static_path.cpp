#include "http3/router/router_node_static_path.h"
#include "http3/router/util.h"

namespace quicx {
namespace http3 {

RouterNodeStaticPath::RouterNodeStaticPath(
    RouterNodeType type, const std::string& section, const std::string& full_path, const RouteConfig& config):
    RouterNode(type, section, full_path, config) {}

bool RouterNodeStaticPath::Match(
    const std::string& path, int path_offset, const std::string& /*cur_section*/, MatchResult& result) {
    // check match done
    if (path_offset < 0 || static_cast<size_t>(path_offset) >= path.length()) {
        // match done, current node is the last node
        // P1-H8: handler presence is decided by HasRouteHandler() (a middle
        // node upgraded by a shorter route registration also matches), not by
        // the creation-time node type.
        if (HasRouteHandler()) {
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
