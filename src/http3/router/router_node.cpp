#include "http3/router/util.h"
#include "http3/router/router_node.h"
#include "http3/router/router_node_wildcard.h"
#include "http3/router/router_node_static_path.h"
#include "http3/router/router_node_dynamic_param.h"

namespace quicx {
namespace http3 {

RouterNode::RouterNode(RouterNodeType type, const std::string& section,
        const std::string& full_path, const http_handler& handler):
    type_(type),
    section_(section),
    full_path_(full_path),
    handler_(handler) {

}

bool RouterNode::AddRoute(const std::string& path, int path_offset, const http_handler& handler) {
    std::string section = PathParse(path, path_offset);
    if (section.empty()) {
        return false;
    }

    std::shared_ptr<IRouterNode> cur_node;

    // find static path first
    auto iter = static_path_map_.find(section);
    if (iter != static_path_map_.end()) {
        cur_node = iter->second;
    } 

    // find dynamic param second
    if (!cur_node) {
        auto iter = dynamic_param_map_.find(section);
        if (iter != dynamic_param_map_.end()) {
            cur_node = iter->second;
        } 
    }

    // create new node
    if (!cur_node) {
        cur_node = MakeNode(path, path_offset, section, handler);
        if (!cur_node) {
            return false;
        }

        switch (cur_node->GetNodeType()) {
        case RouterNodeType::RNT_STATIC_PATH:
        case RouterNodeType::RNT_STATIC_MIDDLE_PATH:
            static_path_map_[section] = cur_node;
            break;
        
        case RouterNodeType::RNT_DYNAMIC_PARAM:
        case RouterNodeType::RNT_DYNAMIC_MIDDLE_PARAM:
            dynamic_param_map_[section] = cur_node;
            break;

        case RouterNodeType::RNT_WILDCARD:
            wildcard_node_ = cur_node;
            break;

        default:
            return false;
        }
    }
    
    if (path_offset >= path.size()) {
        return true;
    }
    return cur_node->AddRoute(path, path_offset, handler);
}

bool RouterNode::Match(const std::string& path, int path_offset, const std::string& cur_section, MatchResult& result) {
    // find static path first
    auto iter = static_path_map_.find(cur_section);
    if (iter != static_path_map_.end()) {
        if (iter->second->Match(path, path_offset, cur_section, result)) {
            return true;
        }
    } else {
        result.handler = nullptr;
        result.is_match = false;
    }

    // find dynamic param second
    for (auto& node : dynamic_param_map_) {
        if (node.second->Match(path, path_offset, cur_section, result)) {
            return true;

        } else {
            result.handler = nullptr;
            result.is_match = false;
        }
    }

    // find wildcard last
    if (wildcard_node_) {
        if(wildcard_node_->Match(path, path_offset, cur_section, result)) {
            return true;

        } else {
            result.handler = nullptr;
            result.is_match = false;
        }
    }
    return false;
}

std::shared_ptr<IRouterNode> RouterNode::MakeNode(const std::string& path, int path_offset,
    const std::string& section, const http_handler& handler) {
    if (section[0] != '/') {
        return nullptr;
    }
    
    bool is_last = path.size() <= path_offset;
    
    RouterNodeType node_type;
    std::string full_path = path.substr(0, path_offset);

    if (section.size() == 1) {
        if (!is_last) {
            // there is nothing between //
            return nullptr;
        }
        return std::make_shared<RouterNodeStaticPath>(RouterNodeType::RNT_STATIC_PATH, section, full_path, handler);

    } else {
        switch (section[1]) {
        case ':':
            node_type = is_last ? RouterNodeType::RNT_DYNAMIC_PARAM : RouterNodeType::RNT_DYNAMIC_MIDDLE_PARAM;
            return std::make_shared<RouterNodeDynamicParam>(node_type, section, full_path, handler);

        case '*':
            if (!is_last) {
                return nullptr; // only support last wildcard
            }
            return std::make_shared<RouterNodeWildcard>(section, full_path, handler);

        default:
            node_type = is_last ? RouterNodeType::RNT_STATIC_PATH : RouterNodeType::RNT_STATIC_MIDDLE_PATH;
            return std::make_shared<RouterNodeStaticPath>(node_type, section, full_path, handler);
        }
    }
    return nullptr;
}

}
}

