#ifndef HTTP3_INCLUDE_TYPE
#define HTTP3_INCLUDE_TYPE

#include <memory>
#include <string>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace quicx {
namespace http3 {

// http method
enum class HttpMethod: uint16_t {
    kGet     = 0x0001,
    kHead    = 0x0002,
    kPost    = 0x0004,
    kPut     = 0x0008,
    kDelete  = 0x0010,
    kConnect = 0x0020,
    kOptions = 0x0040,
    kTrace   = 0x0080,
    kPatch   = 0x0100,
    kAny     = kGet|kHead|kPost|kPut|kDelete|kConnect|kOptions|kTrace|kPatch,
};

// middleware position
enum class MiddlewarePosition: uint8_t {
    kBefore = 0x01,
    kAfter  = 0x02,
};

// log level
enum class LogLevel: uint8_t {
    kNull   = 0x00, // not print log
    kFatal  = 0x01,
    kError  = 0x02 | kFatal,
    kWarn   = 0x04 | kError,
    kInfo   = 0x08 | kWarn,
    kDebug  = 0x10 | kInfo,
};

// http3 settings
struct Http3Settings {
    uint64_t max_header_list_size = 100;
    uint64_t enable_push = 0;
    uint64_t max_concurrent_streams = 100;
    uint64_t max_frame_size = 16384;
    uint64_t max_field_section_size = 16384;
};
static const Http3Settings kDefaultHttp3Settings;

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
