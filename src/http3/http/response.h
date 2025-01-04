#ifndef HTTP3_HTTP_RESPONSE
#define HTTP3_HTTP_RESPONSE

#include <string>
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
    virtual const std::unordered_map<std::string, std::string>& GetHeaders() const { return headers_; }

    // Response body
    virtual void SetBody(const std::string& body) { body_ = body; }
    virtual std::string GetBody() const { return body_; }

private:
    uint32_t status_code_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
};

}
}

#endif
