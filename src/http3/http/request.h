#ifndef HTTP3_HTTP_REQUEST
#define HTTP3_HTTP_REQUEST

#include <string>
#include <unordered_map>
#include "http3/include/type.h"
#include "http3/include/if_request.h"

namespace quicx {
namespace http3 {

class Request:
    public IRequest {
public:
    Request(): request_body_provider_(nullptr), response_body_consumer_(nullptr) {}
    virtual ~Request() {}

    // Set request method (GET, POST, etc)
    virtual void SetMethod(HttpMethod method) { method_ = method; }
    virtual HttpMethod GetMethod() const { return method_; }
    virtual std::string GetMethodString() const;

    // Request path (without query string)
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
    
    // Request body sending (client)
    virtual void SetBody(const std::string& body) { body_ = body; }
    virtual const std::string& GetBody() const { return body_; }
    virtual void SetRequestBodyProvider(const body_provider& provider) { request_body_provider_ = provider; }
    virtual body_provider GetRequestBodyProvider() const { return request_body_provider_; }
    
    // Response body receiving (client)
    virtual void SetResponseBodyConsumer(const body_consumer& consumer) { response_body_consumer_ = consumer; }
    virtual body_consumer GetResponseBodyConsumer() const { return response_body_consumer_; }

    // Query parameters (parsed from :path by URLHelper::ParseQueryParams)
    virtual void SetQueryParams(const std::unordered_map<std::string, std::string>& params) { query_params_ = params; }
    virtual const std::unordered_map<std::string, std::string>& GetQueryParams() const { return query_params_; }

    // Path parameters (set by router during pattern matching)
    virtual void SetPathParams(const std::unordered_map<std::string, std::string>& params) { path_params_ = params; }
    virtual const std::unordered_map<std::string, std::string>& GetPathParams() const { return path_params_; }

private:
    HttpMethod method_;
    std::string path_;
    std::string scheme_;
    std::string authority_;

    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
    body_provider request_body_provider_;    // For streaming request body sending (client)
    body_consumer response_body_consumer_;   // For streaming response body receiving (client)
    
    std::unordered_map<std::string, std::string> query_params_;  // Parsed from ?key=value
    std::unordered_map<std::string, std::string> path_params_;   // Extracted from /users/:id pattern
};

}
}

#endif