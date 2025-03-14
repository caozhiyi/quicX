#include "upgrade/http/http_request.h"

namespace quicx {
namespace upgrade {

HttpRequest::HttpRequest() {

}

HttpRequest::~HttpRequest() {

}

void HttpRequest::SetMethod(const std::string& method) {
    method_ = method;
}
    
void HttpRequest::SetPath(const std::string& path) {
    path_ = path;
}
    
void HttpRequest::AddHeader(const std::string& key, const std::string& value) {
    headers_[key] = value;
}

std::string HttpRequest::GetMethod() const {
    return method_;
}

std::string HttpRequest::GetPath() const {
    return path_;
}
    
const std::unordered_map<std::string, std::string>& HttpRequest::GetHeaders() const {
    return headers_;
}

}
}
