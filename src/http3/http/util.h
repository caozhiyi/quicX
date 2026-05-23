#ifndef HTTP3_HTTP_UTIL
#define HTTP3_HTTP_UTIL

#include <string>
#include <quicx/http3/type.h>

namespace quicx {
namespace http3 {

std::string HttpMethodToString(HttpMethod method);

}
}

#endif
