#ifndef HTTP3_INCLUDE_IF_SERVER
#define HTTP3_INCLUDE_IF_SERVER

#include <string>
#include <memory>
#include <cstdint>
#include "http3/include/type.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"

namespace quicx {
namespace http3 {

struct Http3ServerConfig {
    std::string cert_file_;
    std::string key_file_;
    const char* cert_pem_ = nullptr;
    const char* key_pem_ = nullptr;

    Http3Config config_;
};

// HTTP3 server interface
class IServer {
public:
    IServer() {}
    virtual ~IServer() {};

    // Initialize the server with a certificate and a key
    virtual bool Init(const Http3ServerConfig& config) = 0;

    // Start the server on the given address and port
    // server will block until the server is stopped
    virtual bool Start(const std::string& addr, uint16_t port) = 0;

    // Stop the server
    virtual void Stop() = 0;

    virtual void Join() = 0;

    // Register a handler for a specific path
    virtual void AddHandler(HttpMethod mothed, const std::string& path, const http_handler& handler) = 0;

    // Register a middleware for a specific mothed and position
    virtual void AddMiddleware(HttpMethod mothed, MiddlewarePosition mp, const http_handler& handler) = 0;

    // Create a server instance
    static std::unique_ptr<IServer> Create(const Http3Settings& settings = kDefaultHttp3Settings);
};

}
}

#endif

