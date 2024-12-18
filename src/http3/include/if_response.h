#ifndef HTTP3_HTTP_IF_RESPONSE
#define HTTP3_HTTP_IF_RESPONSE

namespace quicx {
namespace http3 {

// Interface for HTTP3 responses
class IResponse {
public:
    IResponse() {}
    virtual ~IResponse() {}

    // Status code and reason
    virtual void SetStatusCode(uint32_t status_code) = 0;
    virtual uint32_t GetStatusCode() const = 0;

    // Headers
    virtual void AddHeader(const std::string& name, const std::string& value) = 0;
    virtual bool GetHeader(const std::string& name, std::string& value) const = 0;
    virtual const std::unordered_map<std::string, std::string>& GetHeaders() const = 0;

    // Response body
    virtual void SetBody(const std::string& body) = 0;
    virtual std::string GetBody() const = 0;
};

}
}

#endif
