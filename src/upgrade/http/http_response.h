#ifndef UPGRADE_HTTP_HTTP_REQUEST
#define UPGRADE_HTTP_HTTP_REQUEST

#include <string>
#include <unordered_map>

namespace quicx {
namespace upgrade {

class HttpResponse {
public:
    HttpResponse() {}
    virtual ~HttpResponse() {}

    void SetStatusCode(const std::string& status_code);
    void AddHeader(const std::string& key, const std::string& value);
    void SetBody(const std::string& body);

    std::string GetStatusCode() const;
    const std::unordered_map<std::string, std::string>& GetHeaders() const;
    std::string GetBody() const;

private:
    std::string status_code_;
    std::string body_;
    std::unordered_map<std::string, std::string> headers_;
};
}
}

#endif
