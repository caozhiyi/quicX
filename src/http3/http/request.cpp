#include <algorithm>

#include "common/buffer/multi_block_buffer.h"
#include "http3/http/request.h"
#include "http3/http/util.h"
#include "quic/quicx/global_resource.h"

namespace quicx {

std::shared_ptr<IRequest> IRequest::Create() {
    return std::make_shared<http3::Request>();
}

namespace http3 {

// Helper function to convert header name to lowercase (HTTP/2 and HTTP/3 requirement)
static std::string ToLowerCase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::string Request::GetMethodString() const {
    return HttpMethodToString(method_);
}

void Request::AddHeader(const std::string& name, const std::string& value) {
    // HTTP/2 and HTTP/3 require header names to be lowercase
    headers_[ToLowerCase(name)] = value;
}

bool Request::GetHeader(const std::string& name, std::string& value) const {
    // Search with lowercase key for case-insensitive matching
    auto it = headers_.find(ToLowerCase(name));
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
    return body_->GetDataAsString();
}

void Request::AppendBody(const std::string& body) {
    if (body.empty()) {
        return;
    }
    AppendBody(reinterpret_cast<const uint8_t*>(body.data()), static_cast<uint32_t>(body.size()));
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

}  // namespace http3
}  // namespace quicx
