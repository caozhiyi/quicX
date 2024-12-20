#ifndef HTTP3_INCLUDE_IF_SERVER
#define HTTP3_INCLUDE_IF_SERVER

#include <string>
#include <cstdint>
#include "if_request.h"
#include "if_response.h"

namespace quicx {
namespace http3 {

// HTTP3 server interface
class IServer {
public:
    IServer() {}
    virtual ~IServer() = default;

    // Initialize the server with a certificate and a key
    virtual bool Init(const std::string& cert, const std::string& key,
                       uint16_t thread_num) = 0;

    // Start the server on the given address and port
    // server will block until the server is stopped
    virtual bool Start(const std::string& addr, uint16_t port) = 0;

    // Stop the server
    virtual void Stop() = 0;

    // Register a handler for a specific path
    virtual void AddHandler(HttpMothed mothed, const std::string& path, const http_handler& handler) = 0;

    // Register a middleware for a specific mothed and position
    virtual void AddMiddleware(HttpMothed mothed, MiddlewarePosition mp, const http_handler& handler) = 0;
};

}
}

#endif
