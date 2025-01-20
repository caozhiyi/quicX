#ifndef HTTP3_HTTP_RESPONSE
#define HTTP3_HTTP_RESPONSE

#include <string>
#include <vector>
#include <unordered_map>
#include "http3/include/if_response.h"

namespace quicx {
namespace http3 {

class Response:
    public IResponse {
public:
    Response(): status_code_(200) {}
    virtual ~Response() {}

    // Set/get status code
    virtual void SetStatusCode(uint32_t code) { status_code_ = code; }
    virtual uint32_t GetStatusCode() const { return status_code_; }

    // Headers
    virtual void AddHeader(const std::string& name, const std::string& value);
    virtual bool GetHeader(const std::string& name, std::string& value) const;

    virtual void SetHeaders(const std::unordered_map<std::string, std::string>& headers);
    virtual std::unordered_map<std::string, std::string>& GetHeaders() { return headers_; }

    // Response body
    virtual void SetBody(const std::string& body) { body_ = body; }
    virtual const std::string& GetBody() const { return body_; }

    // server push response, can be called multiple times
    // if push response is not enabled, it will be ignored
    virtual void AppendPush(std::shared_ptr<IResponse> response);
    virtual std::vector<std::shared_ptr<IResponse>>& GetPushResponses() { return push_responses_; }

private:
    uint32_t status_code_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;

    std::vector<std::shared_ptr<IResponse>> push_responses_;
};

}
}

#endif