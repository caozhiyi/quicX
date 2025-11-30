#ifndef HTTP3_INCLUDE_TYPE
#define HTTP3_INCLUDE_TYPE

#include <memory>
#include <string>
#include <cstdint>
#include <functional>
#include <unordered_map>

#include "common/include/type.h"

namespace quicx {

/**
 * @brief HTTP method
 * 
 * This method is used to set the HTTP method.
 */
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

/**
 * @brief Middleware position
 * 
 * This position is used to register a middleware for a specific method and position.
 */
enum class MiddlewarePosition: uint8_t {
    kBefore = 0x01,
    kAfter  = 0x02,
};

/**
 * @brief HTTP3 configuration
 *
 * This configuration is used to initialize the HTTP3 client and server.
 */
struct Http3Config {
    uint16_t thread_num_ = 1;                // the number of threads to handle requests
    LogLevel log_level_ = LogLevel::kNull;   // log level

    bool enable_ecn_ = false;                // enable ecn

    /**
     * @brief Connection timeout in milliseconds
     *
     * This timeout controls how long the connection can exist from the initial
     * handshake phase. Set to 0 to disable and rely on idle timeout only.
     *
     * Default: 0 (no timeout, rely on idle timeout mechanism)
     *
     * Note: This is different from idle timeout. Connection timeout starts
     * from connection creation, while idle timeout resets on each activity.
     */
    uint32_t connection_timeout_ms_ = 0;     // 0 = no timeout (rely on idle timeout)
};

/**
 * @brief HTTP3 settings
 * 
 * This settings is used to initialize the HTTP3 client and server.
 */
struct Http3Settings {
    uint64_t max_header_list_size = 100;      // max header list size
    uint64_t enable_push = 0;                 // enable push
    uint64_t max_concurrent_streams = 200;    // max concurrent streams
    uint64_t max_frame_size = 16384;          // max frame size
    uint64_t max_field_section_size = 16384;  // max field section size
    uint64_t qpack_max_table_capacity = 0;    // qpack max table capacity
    uint64_t qpack_blocked_streams = 0;       // qpack blocked streams
};
static const Http3Settings kDefaultHttp3Settings;

class IRequest;
class IResponse;

// ========== Body callback definitions (internal use) ==========
/**
 * @brief Body Consumer callback (internal use)
 * 
 * Used internally by the library to pass body chunks to async handlers.
 * Users should implement IAsyncServerHandler or IAsyncClientHandler instead
 * of using this callback directly.
 * 
 * @param data Pointer to the chunk data
 * @param length Length of the chunk
 * @param is_last True if this is the last chunk, false otherwise
 */
typedef std::function<void(
    const uint8_t* data,   // data pointer
    size_t length,         // data length
    bool is_last           // whether is the last chunk
)> body_consumer;

/**
 * @brief Body Provider callback: provide body data to send
 * 
 * The library pulls data from user by calling this callback repeatedly
 * to obtain body data for sending.
 * 
 * @param buffer Buffer provided by library to fill with data
 * @param buffer_size Size of the buffer
 * @return Actual number of bytes provided, 0 means end of body
 * 
 * @note The provider should:
 *       - Fill the buffer with up to buffer_size bytes
 *       - Return the actual number of bytes filled
 *       - Return 0 to indicate end of body
 *       - NEVER return more than buffer_size
 * 
 * @example
 * @code
 * FILE* file = fopen("data.bin", "rb");
 * request->SetRequestBodyProvider([file](uint8_t* buf, size_t size) {
 *     size_t read = fread(buf, 1, size, file);
 *     if (read == 0) fclose(file);
 *     return read;
 * });
 * @endcode
 */
typedef std::function<size_t(
    uint8_t* buffer,       // buffer provided by library
    size_t buffer_size     // buffer size
)> body_provider;

// ========== Handler definitions ==========
/**
 * @brief HTTP handler callback for complete mode request/response processing
 * 
 * This handler is used for server-side request processing when the entire
 * request body is buffered before the handler is invoked.
 * 
 * @param request The HTTP request object with complete body available via GetBody()
 * @param response The HTTP response object to configure
 * 
 * @note This handler is called AFTER the entire request body has been received
 *       and buffered. Use request->GetBody() to access the complete body.
 * @note For streaming large bodies, use IAsyncServerHandler instead.
 * 
 * @example
 * @code
 * server->AddHandler(HttpMethod::kPost, "/api/data", 
 *     [](std::shared_ptr<IRequest> req, std::shared_ptr<IResponse> resp) {
 *         std::string body = req->GetBody();  // Complete body available
 *         process(body);
 *         resp->SetBody("OK");
 *     });
 * @endcode
 */
typedef std::function<void(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response)> http_handler;

/**
 * @brief HTTP response handler callback for complete mode client response handling
 * 
 * This handler is used for client-side response processing when the entire
 * response body is buffered before the handler is invoked.
 * 
 * @param response The HTTP response object with complete body available via GetBody()
 * @param error Error code (0 means success, non-zero indicates network/protocol error)
 * 
 * @note This handler is called AFTER the entire response body has been received
 *       and buffered. Use response->GetBody() to access the complete body.
 * @note For streaming large responses, use IAsyncClientHandler instead.
 * 
 * @example
 * @code
 * auto req = IRequest::Create();
 * client->DoRequest(url, HttpMethod::kGet, req,
 *     [](std::shared_ptr<IResponse> resp, uint32_t error) {
 *         if (error == 0) {
 *             std::string body = resp->GetBody();  // Complete body available
 *             process(body);
 *         }
 *     });
 * @endcode
 */
typedef std::function<void(std::shared_ptr<IResponse> response, uint32_t error)> http_response_handler;

/**
 * @brief HTTP push promise handler callback
 * 
 * @param headers The push promise headers
 * @return true to accept push, false to cancel push
 */
typedef std::function<bool(std::unordered_map<std::string, std::string>& headers)> http_push_promise_handler;

/**
 * @brief Error handler callback
 * 
 * @param unique_id The unique id of the connection
 * @param error_code The error code
 */
typedef std::function<void(const std::string& unique_id, uint32_t error_code)> error_handler;

}

#endif
