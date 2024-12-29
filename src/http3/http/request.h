#ifndef HTTP3_HTTP_REQUEST
#define HTTP3_HTTP_REQUEST

#include <string>
#include <unordered_map>
#include "http3/include/if_request.h"

namespace quicx {
namespace http3 {

class Request:
    public IRequest {
public:
    Request() {}
    virtual ~Request() {}

    // Set request method (GET, POST, etc)
    virtual void SetMethod(HttpMothed method) { method_ = method; }
    virtual HttpMothed GetMethod() const { return method_; }

    // Request path
    virtual void SetPath(const std::string& path) { path_ = path; }
    virtual std::string GetPath() const { return path_; }

    // Headers
    virtual void AddHeader(const std::string& name, const std::string& value);
    virtual bool GetHeader(const std::string& name, std::string& value) const;
    virtual void SetHeaders(const std::unordered_map<std::string, std::string>& headers) { headers_ = headers; }
    virtual const std::unordered_map<std::string, std::string>& GetHeaders() const { return headers_; }
    
    // Request body
    virtual void SetBody(const std::string& body) { body_ = body; }
    virtual std::string GetBody() const { return body_; }

private:
    HttpMothed method_;
    std::string path_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
};

}
}

#endif
