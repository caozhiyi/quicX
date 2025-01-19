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
    virtual ~IClient() {};

    // thread_num: the number of threads to handle requests
    virtual bool Init(uint16_t thread_num = 1, LogLevel level = LL_NULL) = 0;

    // Send a request to the server
    virtual bool DoRequest(const std::string& url, HttpMethod mothed,
        std::shared_ptr<IRequest> request, const http_response_handler& handler) = 0;

    // Create a client instance
    static std::unique_ptr<IClient> Create();
};

}
}

#endif
