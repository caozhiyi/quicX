#include <string>

#include "common/http/url.h"
#include "common/log/log.h"

#include "http3/stream/pseudo_header.h"

namespace quicx {
namespace http3 {

PseudoHeader::PseudoHeader() {
    // Initialize request pseudo-headers
    request_pseudo_headers_.push_back(PSEUDO_HEADER_METHOD);
    request_pseudo_headers_.push_back(PSEUDO_HEADER_SCHEME);
    request_pseudo_headers_.push_back(PSEUDO_HEADER_AUTHORITY);
    request_pseudo_headers_.push_back(PSEUDO_HEADER_PATH);

    // Initialize response pseudo-headers
    response_pseudo_headers_.push_back(PSEUDO_HEADER_STATUS);
}

PseudoHeader::~PseudoHeader() {}

void PseudoHeader::EncodeRequest(std::shared_ptr<IRequest> request) {
    // Add pseudo-headers
    request->SetHeader(PSEUDO_HEADER_METHOD, MethodToString(request->GetMethod()));
    request->SetHeader(PSEUDO_HEADER_PATH, request->GetPath());
    request->SetHeader(PSEUDO_HEADER_SCHEME, request->GetScheme());
    request->SetHeader(PSEUDO_HEADER_AUTHORITY, request->GetAuthority());
}

void PseudoHeader::DecodeRequest(std::shared_ptr<IRequest> request) {
    // Headers are an ordered field-line sequence (RFC 9110 §5.3), so pseudo
    // headers are read out and the remainder is written back via SetHeaders().
    const HttpFields& fields = request->GetHeaders();

    auto take = [&fields](const std::string& name, std::string* out) -> bool {
        for (const auto& field : fields) {
            if (field.first == name) {
                if (out) *out = field.second;
                return true;
            }
        }
        return false;
    };

    std::string value;
    if (take(PSEUDO_HEADER_METHOD, &value)) {
        request->SetMethod(StringToMethod(value));
    }

    if (take(PSEUDO_HEADER_PATH, &value)) {
        std::string path;
        std::unordered_map<std::string, std::string> query_params;
        if (common::ParsePathWithQuery(value, path, query_params)) {
            request->SetPath(path);
            if (!query_params.empty()) {
                request->SetQueryParams(query_params);
            }
        } else {
            request->SetPath(value);
        }
    }

    if (take(PSEUDO_HEADER_SCHEME, &value)) {
        request->SetScheme(value);
    }

    if (take(PSEUDO_HEADER_AUTHORITY, &value)) {
        request->SetAuthority(value);
    }

    // Drop pseudo-header field lines, preserving the order of the rest.
    HttpFields rest;
    rest.reserve(fields.size());
    for (const auto& field : fields) {
        if (!field.first.empty() && field.first[0] == ':') continue;
        rest.push_back(field);
    }
    request->SetHeaders(rest);
}

void PseudoHeader::EncodeResponse(std::shared_ptr<IResponse> response) {
    // Add status pseudo-header
    response->SetHeader(PSEUDO_HEADER_STATUS, std::to_string(response->GetStatusCode()));
}

void PseudoHeader::DecodeResponse(std::shared_ptr<IResponse> response) {
    const HttpFields& fields = response->GetHeaders();

    for (const auto& field : fields) {
        if (field.first == PSEUDO_HEADER_STATUS) {
            // Peer-controlled value: std::stoul throws std::invalid_argument /
            // std::out_of_range on non-numeric or oversized input and the
            // exception would unwind through the event loop and abort the
            // process (remote crash, P0-S7). Parse manually instead.
            uint32_t status = 0;
            bool ok = !field.second.empty() && field.second.size() <= 3;
            for (char c : field.second) {
                if (c < '0' || c > '9') {
                    ok = false;
                    break;
                }
                status = status * 10 + static_cast<uint32_t>(c - '0');
            }
            if (ok && status >= 100 && status <= 599) {
                response->SetStatusCode(status);
            } else {
                LOG_WARN("ignoring malformed :status pseudo-header value: '%s'", field.second.c_str());
            }
            break;
        }
    }

    HttpFields rest;
    rest.reserve(fields.size());
    for (const auto& field : fields) {
        if (!field.first.empty() && field.first[0] == ':') continue;
        rest.push_back(field);
    }
    response->SetHeaders(rest);
}

std::string PseudoHeader::MethodToString(HttpMethod method) {
    auto iter = kMethodToStringMap.find(method);
    if (iter != kMethodToStringMap.end()) {
        return iter->second;
    }

    LOG_FATAL("Invalid method: %d", method);
    return "";
}

HttpMethod PseudoHeader::StringToMethod(const std::string& method) {
    auto iter = kStringToMethodMap.find(method);
    if (iter != kStringToMethodMap.end()) {
        return iter->second;
    }

    // Peer-controlled token: never FATAL on it and never fall back to kGet
    // (that would mis-route extension methods like "FOO" into GET handlers).
    // kUnknown is outside kAny's bitmask, so route matching rejects the
    // request instead (RFC 9114 §4.1.2 extension methods).
    LOG_WARN("unknown http method token in :method pseudo-header: '%s'", method.c_str());
    return HttpMethod::kUnknown;
}

const std::unordered_map<std::string, HttpMethod> PseudoHeader::kStringToMethodMap = {
    {"GET", HttpMethod::kGet},
    {"HEAD", HttpMethod::kHead},
    {"POST", HttpMethod::kPost},
    {"PUT", HttpMethod::kPut},
    {"DELETE", HttpMethod::kDelete},
    {"CONNECT", HttpMethod::kConnect},
    {"OPTIONS", HttpMethod::kOptions},
    {"TRACE", HttpMethod::kTrace},
    {"PATCH", HttpMethod::kPatch},
    // NOTE: no "ANY" here — kAny is an internal routing wildcard bitmask, not
    // an HTTP method token. Accepting ":method: ANY" from a peer used to map
    // an attacker-chosen token straight onto the wildcard route matcher
    // (P0-S7). Outbound encoding below still maps kAny to "ANY" for local
    // API compatibility; peers see it as an (unroutable) extension method.
};

const std::unordered_map<HttpMethod, std::string> PseudoHeader::kMethodToStringMap = {
    {HttpMethod::kGet, "GET"},
    {HttpMethod::kHead, "HEAD"},
    {HttpMethod::kPost, "POST"},
    {HttpMethod::kPut, "PUT"},
    {HttpMethod::kDelete, "DELETE"},
    {HttpMethod::kConnect, "CONNECT"},
    {HttpMethod::kOptions, "OPTIONS"},
    {HttpMethod::kTrace, "TRACE"},
    {HttpMethod::kPatch, "PATCH"},
    {HttpMethod::kAny, "ANY"},
};

}  // namespace http3
}  // namespace quicx
