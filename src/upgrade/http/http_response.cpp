#include "upgrade/http/http_response.h"

namespace quicx {
namespace upgrade {

HttpResponse::HttpResponse() {

}

HttpResponse::~HttpResponse() {

}

void HttpResponse::SetStatusCode(const std::string& status_code) {
    status_code_ = status_code;
}

void HttpResponse::AddHeader(const std::string& key, const std::string& value) {
    headers_[key] = value;
}

void HttpResponse::SetBody(const std::string& body) {
    body_ = body;
}

std::string HttpResponse::GetStatusCode() const {
    return status_code_;
}

const std::unordered_map<std::string, std::string>& HttpResponse::GetHeaders() const {
    return headers_;
}

std::string HttpResponse::GetBody() const {
    return body_;
}

}
}