#ifndef HTTP3_INCLUDE_TYPE
#define HTTP3_INCLUDE_TYPE

#include <memory>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace quicx {
namespace http3 {

enum HttpMethod: uint16_t {
    HM_GET     = 0x0001,
    HM_HEAD    = 0x0002,
    HM_POST    = 0x0004,
    HM_PUT     = 0x0008,
    HM_DELETE  = 0x0010,
    HM_CONNECT = 0x0020,
    HM_OPTIONS = 0x0040,
    HM_TRACE   = 0x0080,
    HM_PATCH   = 0x0100,
    HM_ANY     = HM_GET|HM_HEAD|HM_POST|HM_PUT|HM_DELETE|HM_CONNECT|HM_OPTIONS|HM_TRACE|HM_PATCH,
};

enum MiddlewarePosition: uint8_t {
    MP_BEFORE = 0x01,
    MP_AFTER  = 0x02,
};

enum LogLevel: uint8_t {
    LL_NULL         = 0x00, // not print log
    LL_FATAL        = 0x01,
    LL_ERROR        = 0x02 | LL_FATAL,
    LL_WARN         = 0x04 | LL_ERROR,
    LL_INFO         = 0x08 | LL_WARN,
    LL_DEBUG        = 0x10 | LL_INFO,
};

class IRequest;
class IResponse;

// http handler
typedef std::function<void(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response)> http_handler;
// http response handler, error is 0 means success, otherwise means error
typedef std::function<void(std::shared_ptr<IResponse> response, uint32_t error)> http_response_handler;
// http push promise handler, return true means do not cancel push, return false means cancel push
typedef std::function<bool(std::unordered_map<std::string, std::string>& headers)> http_push_promise_handler;

}
}

#endif
