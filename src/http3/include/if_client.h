#ifndef HTTP3_INCLUDE_IF_CLIENT
#define HTTP3_INCLUDE_IF_CLIENT

#include <string>
#include <memory>
#include <cstdint>
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"

namespace quicx {
namespace http3 {

// HTTP3 client interface
class IClient {
public:
    IClient() {}
    virtual ~IClient() = default;

    // Initialize the server with a certificate and a key
    virtual bool Init(uint16_t thread_num) = 0;

    // Send a request to the server
    virtual bool DoRequest(const std::string& url, const IRequest& request, const http_response_handler& handler) = 0;
};

}
}

#endif
