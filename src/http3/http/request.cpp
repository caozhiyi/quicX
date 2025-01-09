#include "http3/http/request.h"

namespace quicx {
namespace http3 {

std::shared_ptr<IRequest> IRequest::Create() {
    return std::make_shared<Request>();
}

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
