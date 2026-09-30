#include <algorithm>

#include "common/buffer/multi_block_buffer.h"

#include "quic/quicx/global_resource.h"

#include "http3/http/response.h"

namespace quicx {

std::shared_ptr<IResponse> IResponse::Create() {
    return std::make_shared<http3::Response>();
}

namespace http3 {

// Helper function to convert header name to lowercase (HTTP/2 and HTTP/3 requirement)
static std::string ToLowerCase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    return result;
}

void Response::AddHeader(const std::string& name, const std::string& value) {
    // HTTP/2 and HTTP/3 require header names to be lowercase.
    // Append (not replace): RFC 9110 §5.3 — repeated names are legal and
    // Set-Cookie in particular MUST NOT be collapsed into one field line.
    headers_.emplace_back(ToLowerCase(name), value);
}

void Response::SetHeader(const std::string& name, const std::string& value) {
    std::string key = ToLowerCase(name);
    HttpFields kept;
    kept.reserve(headers_.size() + 1);
    for (auto& field : headers_) {
        if (field.first != key) kept.push_back(field);
    }
    kept.emplace_back(key, value);
    headers_.swap(kept);
}

bool Response::GetHeader(const std::string& name, std::string& value) const {
    // First matching field line wins (case-insensitive name match).
    std::string key = ToLowerCase(name);
    for (const auto& field : headers_) {
        if (field.first == key) {
            value = field.second;
            return true;
        }
    }
    return false;
}

std::vector<std::string> Response::GetAllHeaders(const std::string& name) const {
    std::string key = ToLowerCase(name);
    std::vector<std::string> values;
    for (const auto& field : headers_) {
        if (field.first == key) values.push_back(field.second);
    }
    return values;
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
    AppendBody(reinterpret_cast<const uint8_t*>(body.data()), static_cast<uint32_t>(body.size()));
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

}  // namespace http3
}  // namespace quicx
