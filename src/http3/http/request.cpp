#include "http3/http/request.h"

namespace quicx {
namespace http3 {

void Request::AddHeader(const std::string& name, const std::string& value) { 
    headers_[name] = value; 
}

bool Request::GetHeader(const std::string& name, std::string& value) const {
    auto it = headers_.find(name);
    if (it != headers_.end()) {
        value = it->second;
        return true;
    }
    return false;
}

}
}
