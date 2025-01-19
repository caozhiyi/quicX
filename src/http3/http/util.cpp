#include "http3/http/util.h"

namespace quicx {
namespace http3 {

std::string HttpMethodToString(HttpMethod method) {
    switch (method) {
        case HM_GET:
            return "GET";
        case HM_HEAD:
            return "HEAD";
        case HM_POST:
            return "POST";
        case HM_PUT:
            return "PUT";
        case HM_DELETE:
            return "DELETE";
        case HM_CONNECT:
            return "CONNECT";
        case HM_OPTIONS:
            return "OPTIONS";
        case HM_TRACE:
            return "TRACE";
        case HM_PATCH:
            return "PATCH";
        case HM_ANY:
            return "ANY";
        default:
            return "UNKNOWN";
    }
}

}
}

