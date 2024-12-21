#ifndef HTTP3_INCLUDE_IF_CLIENT
#define HTTP3_INCLUDE_IF_CLIENT

#include <string>
#include <memory>
#include <cstdint>
#include "if_request.h"
#include "if_response.h"

namespace quicx {
namespace http3 {

// HTTP3 client interface
class IClient {
public:
    IClient() {}
    virtual ~IClient() = default;

    // Initialize the server with a certificate and a key
    virtual bool Init(const std::string& cert, const std::string& key,
                       uint16_t thread_num) = 0;

    // Start the client
    virtual bool Start() = 0;

    virtual bool Stop() = 0;

    // Send a request to the server
    virtual bool DoRequest(const IRequest& request, const http_handler handler) = 0;
};

}
}

#endif
