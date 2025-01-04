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
    virtual void SetMethod(HttpMethod method) { method_ = method; }
    virtual HttpMethod GetMethod() const { return method_; }

    // Request path
    virtual void SetPath(const std::string& path) { path_ = path; }
    virtual const std::string& GetPath() const { return path_; }

    // Request scheme
    virtual void SetScheme(const std::string& scheme) { scheme_ = scheme; }
    virtual const std::string& GetScheme() const { return scheme_; }

    // Request authority
    virtual void SetAuthority(const std::string& authority) { authority_ = authority; }
    virtual const std::string& GetAuthority() const { return authority_; }

    // Headers
    virtual void AddHeader(const std::string& name, const std::string& value);
    virtual bool GetHeader(const std::string& name, std::string& value) const;
    virtual void SetHeaders(const std::unordered_map<std::string, std::string>& headers) { headers_ = headers; }
    virtual std::unordered_map<std::string, std::string>& GetHeaders() { return headers_; }
    
    // Request body
    virtual void SetBody(const std::string& body) { body_ = body; }
    virtual const std::string& GetBody() const { return body_; }

private:
    HttpMethod method_;
    std::string path_;
    std::string scheme_;
    std::string authority_;

    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
};

}
}

#endif