#include <algorithm>

#include "http3/http/response.h"
#include "common/buffer/multi_block_buffer.h"
#include "quic/quicx/global_resource.h"


namespace quicx {

std::shared_ptr<IResponse> IResponse::Create() {
    return std::make_shared<http3::Response>();
}

namespace http3 {

// Helper function to convert header name to lowercase (HTTP/2 and HTTP/3 requirement)
static std::string ToLowerCase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

void Response::AddHeader(const std::string& name, const std::string& value) {
    // HTTP/2 and HTTP/3 require header names to be lowercase
    headers_[ToLowerCase(name)] = value;
}

bool Response::GetHeader(const std::string& name, std::string& value) const {
    // Search with lowercase key for case-insensitive matching
    auto it = headers_.find(ToLowerCase(name));
    if (it != headers_.end()) {
        value = it->second;
        return true;
    }
    return false;
}

void Response::SetHeaders(const std::unordered_map<std::string, std::string>& headers) {
    headers_ = headers;
}

std::string Response::GetBodyAsString() const {
    if (!body_) {
        return "";
    }
    return body_->GetDataAsString();
}

void Response::AppendPush(std::shared_ptr<IResponse> response) {
    push_responses_.push_back(response);
}

void Response::AppendBody(const std::string& body) {
    if (body.empty()) {
        return;
    }
    AppendBody(reinterpret_cast<const uint8_t*>(body.data()),
               static_cast<uint32_t>(body.size()));
}

void Response::AppendBody(const uint8_t* data, uint32_t length) {
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
