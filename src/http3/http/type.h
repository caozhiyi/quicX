#ifndef HTTP3_HTTP_TYPE
#define HTTP3_HTTP_TYPE

namespace quicx {
namespace http3 {

static const char* kHttp3Alpn = "h3";

static const int kServerPushWaitTimeMs = 10;
static const int kClientConnectionTimeoutMs = 10000;

}
}

#endif
