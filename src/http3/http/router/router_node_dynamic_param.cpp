#include "http3/http/router/router_node_dynamic_param.h"

namespace quicx {
namespace http3 {

RouterNodeDynamicParam::RouterNodeDynamicParam(RouterNodeType type, const std::string& section,
    const std::string& full_path, const http_handler& handler):
    RouterNode(type, section, full_path, handler) {

    // /:param_name -> param_name
    param_name_ = section.substr(2, section.size() - 2);
}

bool RouterNodeDynamicParam::Match(const std::string& path, int path_offset, const std::string& cur_section, MatchResult& result) {
    // check match done
    if (path_offset >= path.length()) {
        // match done, current node is the last node
        if (type_ == RouterNodeType::RNT_DYNAMIC_PARAM) {
            result.handler = handler_;
            // parse param, /param -> param
            result.params[param_name_] = cur_section.substr(1, cur_section.size() - 1);
            return true;
        }
        
        result.handler = nullptr;
        return false;
    }
    
    // get next section
    std::string section = PathParse(path, path_offset);
    if (section.empty()) {
        return false;
    }

    if (RouterNode::Match(path, path_offset, section, result)) {
        // parse param, /param -> param
        result.params[param_name_] = cur_section.substr(1, cur_section.size() - 1);
        return true;
    }
    return false;
}

}
}
