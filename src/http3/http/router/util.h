#ifndef HTTP3_HTTP_ROUTER_UTIL
#define HTTP3_HTTP_ROUTER_UTIL

#include <string>

namespace quicx {
namespace http3 {

// parse path, return single path and offset
std::string PathParse(const std::string& path, int& offset);

}
}

#endif
