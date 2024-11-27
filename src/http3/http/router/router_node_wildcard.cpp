#include "http3/http/router/if_router_node.h"
#include "http3/http/router/router_node_wildcard.h"

namespace quicx {
namespace http3 {

RouterNodeWildcard::RouterNodeWildcard(const std::string& section,
    const std::string& full_path, const http_handler& handler):
    RouterNode(RouterNodeType::RNT_WILDCARD, section, full_path, handler) {

}

bool RouterNodeWildcard::Match(const std::string& path, int path_offset, const std::string& cur_section, MatchResult& result) {
    result.handler = handler_;

    // wildcard match only for the last part of the path, so we can return directly 
    return true;
}

}
}
