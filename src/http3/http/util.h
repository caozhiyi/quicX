#ifndef HTTP3_HTTP_UTIL
#define HTTP3_HTTP_UTIL

#include <quicx/http3/type.h>
#include <string>

namespace quicx {
namespace http3 {

std::string HttpMethodToString(HttpMethod method);

}
}  // namespace quicx

#endif
