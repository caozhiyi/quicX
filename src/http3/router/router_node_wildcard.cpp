#include "http3/router/if_router_node.h"
#include "http3/router/router_node_wildcard.h"

namespace quicx {
namespace http3 {

RouterNodeWildcard::RouterNodeWildcard(
    const std::string& section, const std::string& full_path, const RouteConfig& config):
    RouterNode(RouterNodeType::RNT_WILDCARD, section, full_path, config) {}

bool RouterNodeWildcard::Match(
    const std::string& path, int path_offset, const std::string& cur_section, MatchResult& result) {
    result.config = config_;
    result.is_match = true;
    return true;
}

}  // namespace http3
}  // namespace quicx
