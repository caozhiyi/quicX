#include "http3/router/router_node_dynamic_param.h"
#include "http3/router/util.h"

namespace quicx {
namespace http3 {

RouterNodeDynamicParam::RouterNodeDynamicParam(
    RouterNodeType type, const std::string& section, const std::string& full_path, const RouteConfig& config):
    RouterNode(type, section, full_path, config) {
    // /:param_name -> param_name
    param_name_ = section.substr(2, section.size() - 2);
}

bool RouterNodeDynamicParam::Match(
    const std::string& path, int path_offset, const std::string& cur_section, MatchResult& result) {
    // check match done
    if (path_offset < 0 || static_cast<size_t>(path_offset) >= path.length()) {
        // match done, current node is the last node
        if (HasRouteHandler()) {  // P1-H8: see router_node_static_path.cpp
            result.config = config_;
            result.is_match = true;
            // parse param, /param -> param
            result.params[param_name_] = cur_section.substr(1, cur_section.size() - 1);
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

    if (RouterNode::Match(path, path_offset, section, result)) {
        // parse param, /param -> param
        result.params[param_name_] = cur_section.substr(1, cur_section.size() - 1);
        return true;
    }
    // P1-H8: undo the parameter bindings a failed subtree may have written
    // into result.params, so backtracking branches (including the kAny-tree
    // fallback in Router::Match) never observe stale entries.
    result.params.erase(param_name_);
    return false;
}

}  // namespace http3
}  // namespace quicx
