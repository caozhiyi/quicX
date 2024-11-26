#include "http/router/router_node.h"

namespace quicx {
namespace http3 {

RouterNode::RouterNode() {

}

RouterNode::~RouterNode() {

}

bool RouterNode::AddRoute(const std::string& path, http_handler handler) {
    RouterNode& cur_node = *this;
    return PathParse(path, [&cur_node, &handler] (const std::string& full_path, const std::string& sub_path) -> bool{
        if (sub_path.empty()) {
            return false;
        }

        if (sub_path[0] == ':') {
            cur_node._param = sub_path.substr(1, sub_path.length());
            return true;
        }
        
        auto iter = cur_node._node_map.find(sub_path);
        if (iter == cur_node._node_map.end()) {
            RouterNode new_node;
            new_node._sub_path = sub_path;
            new_node._full_path = full_path;
            new_node._http_handler = handler;
            cur_node = new_node;

        } else {
            cur_node = iter->second;
        }
        return true;
    });
}

RouterNode* RouterNode::Match(const std::string& uri, std::unordered_map<std::string, std::string>& param) {
    RouterNode* ret = nullptr;
    RouterNode& cur_node = *this;
    if (!PathParse(uri, [&ret, &cur_node, &param] (const std::string&, const std::string& sub_path) -> bool{
        if (sub_path.empty()) {
            return false;
        }

        if (!cur_node._param.empty()) {
            param[cur_node._param] = sub_path;
            return true;
        }
        
        auto iter = cur_node._node_map.find(sub_path);
        if (iter != cur_node._node_map.end()) {
            cur_node = iter->second;
            ret = &cur_node;
            return true;
        }

        return false;
    }));
    
    return ret;
}

bool RouterNode::PathParse(const std::string& path, std::function<bool(const std::string& full_path, const std::string& sub_path)> handle) {
    int32_t sub_str_start = -1;
    int32_t sub_str_end = -1;
    for (size_t i = 0; i < path.size(); i++){
        if (path[i] == '/' && sub_str_start < 0) {
            sub_str_start = i;
        } else {
            sub_str_end = i;
        }
        
        if (sub_str_start >= 0 && sub_str_end >= 0) {
            // sub path must not be null
            if (sub_str_end - sub_str_start < 2) {
                return false;
            }
    
            sub_str_start = sub_str_end = -1;
            if (!handle(path.substr(0, sub_str_end - 1), 
                        path.substr(sub_str_start, sub_str_end - 1 - sub_str_start))) {

                return false;
            }
        }
    }
    return true;
}

}
}
