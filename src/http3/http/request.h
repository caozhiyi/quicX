#ifndef HTTP3_HTTP_REQUEST
#define HTTP3_HTTP_REQUEST

#include <string>
#include <memory>
#include <unordered_map>
#include "http3/include/type.h"
#include "http3/include/if_request.h"
#include "common/buffer/if_buffer.h"

namespace quicx {
namespace http3 {

class Request:
    public IRequest {
public:
    Request(): request_body_provider_(nullptr), response_body_consumer_(nullptr) {}
    virtual ~Request() {}

    // Set request method (GET, POST, etc)
    virtual void SetMethod(HttpMethod method) override { method_ = method; }
    virtual HttpMethod GetMethod() const override { return method_; }
    virtual std::string GetMethodString() const override;

    // Request path (without query string)
    virtual void SetPath(const std::string& path) override { path_ = path; }
    virtual const std::string& GetPath() const override { return path_; }

    // Request scheme
    virtual void SetScheme(const std::string& scheme) override { scheme_ = scheme; }
    virtual const std::string& GetScheme() const override { return scheme_; }

    // Request authority
    virtual void SetAuthority(const std::string& authority) override { authority_ = authority; }
    virtual const std::string& GetAuthority() const override { return authority_; }

    // Headers
    virtual void AddHeader(const std::string& name, const std::string& value) override;
    virtual bool GetHeader(const std::string& name, std::string& value) const override;
    virtual void SetHeaders(const std::unordered_map<std::string, std::string>& headers) override { headers_ = headers; }
    virtual std::unordered_map<std::string, std::string>& GetHeaders() override { return headers_; }
    virtual std::string GetBodyAsString() const override;
    
    // Request body sending (client)
    virtual void SetBody(std::shared_ptr<common::IBuffer> body) { body_ = body; }
    virtual void AppendBody(const std::string& body) override;
    virtual void AppendBody(const uint8_t* data, uint32_t length) override;
    virtual std::shared_ptr<IBufferRead> GetBody() const override { return body_; }
    virtual void SetRequestBodyProvider(const body_provider& provider) override { request_body_provider_ = provider; }
    virtual body_provider GetRequestBodyProvider() const override { return request_body_provider_; }
    
    // Response body receiving (client)
    virtual void SetResponseBodyConsumer(const body_consumer& consumer) { response_body_consumer_ = consumer; }
    virtual body_consumer GetResponseBodyConsumer() const { return response_body_consumer_; }

    // Query parameters (parsed from :path by URLHelper::ParseQueryParams)
    virtual void SetQueryParams(const std::unordered_map<std::string, std::string>& params) { query_params_ = params; }
    virtual const std::unordered_map<std::string, std::string>& GetQueryParams() const override { return query_params_; }

    // Path parameters (set by router during pattern matching)
    virtual void SetPathParams(const std::unordered_map<std::string, std::string>& params) override { path_params_ = params; }
    virtual const std::unordered_map<std::string, std::string>& GetPathParams() const override { return path_params_; }

private:
    HttpMethod method_;
    std::string path_;
    std::string scheme_;
    std::string authority_;

private:
    std::unordered_map<std::string, std::string> headers_;
    std::shared_ptr<common::IBuffer> body_;
    body_provider request_body_provider_;    // For streaming request body sending (client)
    body_consumer response_body_consumer_;   // For streaming response body receiving (client)
    
    std::unordered_map<std::string, std::string> query_params_;  // Parsed from ?key=value
    std::unordered_map<std::string, std::string> path_params_;   // Extracted from /users/:id pattern
};

}
}

#endif