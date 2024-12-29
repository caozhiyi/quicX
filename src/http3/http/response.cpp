#include "http3/http/response.h"

namespace quicx {
namespace http3 {

void Response::AddHeader(const std::string& name, const std::string& value) {
    headers_[name] = value;
}

bool Response::GetHeader(const std::string& name, std::string& value) const {
    auto it = headers_.find(name);
    if (it != headers_.end()) {
        value = it->second;
        return true;
    }
    return false;
}

void Response::SetHeaders(const std::unordered_map<std::string, std::string>& headers) {
    headers_ = headers;
}

}
}
