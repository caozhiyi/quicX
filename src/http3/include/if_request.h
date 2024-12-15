#ifndef HTTP3_HTTP_IF_REQUEST
#define HTTP3_HTTP_IF_REQUEST

#include <string>
#include <unordered_map>

namespace quicx {
namespace http3 {

// Interface for HTTP3 requests
class IRequest {
public:
    IRequest() {}
    virtual ~IRequest() {}

    // HTTP method (GET, POST, etc)
    virtual void SetMethod(const std::string& method) = 0;
    virtual std::string GetMethod() const = 0;

    // Request path
    virtual void SetPath(const std::string& path) = 0;
    virtual std::string GetPath() const = 0;

    // Headers
    virtual void AddHeader(const std::string& name, const std::string& value) = 0;
    virtual bool GetHeader(const std::string& name, std::string& value) const = 0;
    virtual const std::unordered_map<std::string, std::string>& GetHeaders() const = 0;
    
    // Request body
    virtual void SetBody(const std::string& body) = 0;
    virtual std::string GetBody() const = 0;
};

}
}

#endif
