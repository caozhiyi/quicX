#include "http3/http/util.h"
#include "http3/http/request.h"
#include "common/buffer/multi_block_buffer.h"
#include "quic/quicx/global_resource.h"

namespace quicx {

std::shared_ptr<IRequest> IRequest::Create() {
    return std::make_shared<http3::Request>();
}

namespace http3 {

std::string Request::GetMethodString() const {
    return HttpMethodToString(method_);
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

std::string Request::GetBodyAsString() const {
    if (!body_) {
        return "";
    }
    std::string body;
    body.resize(body_->GetDataLength());
    body_->VisitData([&](uint8_t* data, uint32_t length) {
        body.append(reinterpret_cast<char*>(data), length);
    });
    return body;
}


void Request::AppendBody(const std::string& body) {
    if (body.empty()) {
        return;
    }
    AppendBody(reinterpret_cast<const uint8_t*>(body.data()),
               static_cast<uint32_t>(body.size()));
}

void Request::AppendBody(const uint8_t* data, uint32_t length) {
    if (data == nullptr || length == 0) {
        return;
    }
    if (!body_) {
        body_ = std::make_shared<common::MultiBlockBuffer>(quic::GlobalResource::Instance().GetThreadLocalBlockPool());
    }
    body_->Write(data, length);
}

}
}
