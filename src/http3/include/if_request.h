#ifndef HTTP3_HTTP_IF_REQUEST
#define HTTP3_HTTP_IF_REQUEST

#include <string>
#include <memory>
#include <unordered_map>
#include "http3/include/type.h"

namespace quicx {
namespace http3 {

// Interface for HTTP3 requests
class IRequest {
public:
    IRequest() {}
    virtual ~IRequest() = default;

    // HTTP method
    virtual void SetMethod(HttpMothed method) = 0;
    virtual HttpMothed GetMethod() const = 0;

    // Request path
    virtual void SetPath(const std::string& path) = 0;
    virtual std::string GetPath() const = 0;

    // Headers
    virtual void AddHeader(const std::string& name, const std::string& value) = 0;
    virtual bool GetHeader(const std::string& name, std::string& value) const = 0;
    virtual void SetHeaders(const std::unordered_map<std::string, std::string>& headers) = 0;
    virtual const std::unordered_map<std::string, std::string>& GetHeaders() const = 0;
    
    // Request body
    virtual void SetBody(const std::string& body) = 0;
    virtual std::string GetBody() const = 0;

    // Create a request instance
    static std::unique_ptr<IRequest> Create();
};

}
}

#endif

