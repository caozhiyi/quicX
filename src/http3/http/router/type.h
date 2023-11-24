#ifndef HTTP3_HTTP_ROUTER_TYPE
#define HTTP3_HTTP_ROUTER_TYPE

#include <functional>

namespace quicx {
namespace http3 {

enum MothedType {
    MT_GET     = 0x0001,
    MT_HEAD    = 0x0002,
    MT_POST    = 0x0004,
    MT_PUT     = 0x0008,
    MT_DELETE  = 0x0010,
    MT_CONNECT = 0x0020,
    MT_OPTIONS = 0x0040,
    MT_TRACE   = 0x0080,
    MT_PATCH   = 0x0100,
};

class IRequest;
class IResponse;

typedef std::function<void(const IRequest& request, IResponse& response)> http_handler;

}
}

#endif
