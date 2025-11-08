#ifndef HTTP3_HTTP_RESPONSE
#define HTTP3_HTTP_RESPONSE

#include <string>
#include <vector>
#include <unordered_map>
#include "http3/include/type.h"
#include "http3/include/if_response.h"

namespace quicx {
namespace http3 {

class Response:
    public IResponse {
public:
    Response(): status_code_(200), response_body_provider_(nullptr), request_body_consumer_(nullptr) {}
    virtual ~Response() {}

    // Set/get status code
    virtual void SetStatusCode(uint32_t code) { status_code_ = code; }
    virtual uint32_t GetStatusCode() const { return status_code_; }

    // Headers
    virtual void AddHeader(const std::string& name, const std::string& value);
    virtual bool GetHeader(const std::string& name, std::string& value) const;

    virtual void SetHeaders(const std::unordered_map<std::string, std::string>& headers);
    virtual std::unordered_map<std::string, std::string>& GetHeaders() { return headers_; }

    // Response body sending (server)
    virtual void SetBody(const std::string& body) { body_ = body; }
    virtual const std::string& GetBody() const { return body_; }
    virtual void SetResponseBodyProvider(const body_provider& provider) { response_body_provider_ = provider; }
    virtual body_provider GetResponseBodyProvider() const { return response_body_provider_; }
    
    // Request body receiving (server)
    virtual void SetRequestBodyConsumer(const body_consumer& consumer) { request_body_consumer_ = consumer; }
    virtual body_consumer GetRequestBodyConsumer() const { return request_body_consumer_; }

    // server push response, can be called multiple times
    // if push response is not enabled, it will be ignored
    virtual void AppendPush(std::shared_ptr<IResponse> response);
    virtual std::vector<std::shared_ptr<IResponse>>& GetPushResponses() { return push_responses_; }

private:
    uint32_t status_code_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
    body_provider response_body_provider_;    // For streaming response body sending (server)
    body_consumer request_body_consumer_;     // For streaming request body receiving (server)

    std::vector<std::shared_ptr<IResponse>> push_responses_;
};

}
}

#endif