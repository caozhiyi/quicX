#ifndef UPGRADE_HTTP_HTTP_REQUEST
#define UPGRADE_HTTP_HTTP_REQUEST

#include <string>
#include <unordered_map>

namespace quicx {
namespace upgrade {

class HttpRequest {
public:
    HttpRequest() {}
    virtual ~HttpRequest() {}

    void SetMethod(const std::string& method);
    void SetPath(const std::string& path);
    void AddHeader(const std::string& key, const std::string& value);

    std::string GetMethod() const;
    std::string GetPath() const;
    const std::unordered_map<std::string, std::string>& GetHeaders() const;

private:
    std::string method_;
    std::string path_;
    std::unordered_map<std::string, std::string> headers_;
};
}
}

#endif
