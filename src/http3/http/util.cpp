#include "http3/http/util.h"

namespace quicx {
namespace http3 {

std::string HttpMethodToString(HttpMethod method) {
    switch (method) {
        case HttpMethod::kGet:
            return "GET";
        case HttpMethod::kHead:
            return "HEAD";
        case HttpMethod::kPost:
            return "POST";
        case HttpMethod::kPut:
            return "PUT";
        case HttpMethod::kDelete:
            return "DELETE";
        case HttpMethod::kConnect:
            return "CONNECT";
        case HttpMethod::kOptions:
            return "OPTIONS";
        case HttpMethod::kTrace:
            return "TRACE";
        case HttpMethod::kPatch:
            return "PATCH";
        case HttpMethod::kAny:
            return "ANY";
        default:
            return "UNKNOWN";
    }
}

}
}

