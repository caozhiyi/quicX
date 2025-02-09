#include <string>
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

PseudoHeader::~PseudoHeader() {
}

void PseudoHeader::EncodeRequest(std::shared_ptr<IRequest> request) {
    // Add pseudo-headers
    request->AddHeader(PSEUDO_HEADER_METHOD, MethodToString(request->GetMethod()));
    request->AddHeader(PSEUDO_HEADER_PATH, request->GetPath());
    request->AddHeader(PSEUDO_HEADER_SCHEME, request->GetScheme());
    request->AddHeader(PSEUDO_HEADER_AUTHORITY, request->GetAuthority());
}

void PseudoHeader::DecodeRequest(std::shared_ptr<IRequest> request) {
    auto& headers = request->GetHeaders();

    // Extract pseudo-headers
    if (headers.find(PSEUDO_HEADER_METHOD) != headers.end()) {
        request->SetMethod(StringToMethod(headers[PSEUDO_HEADER_METHOD]));
        headers.erase(PSEUDO_HEADER_METHOD);
    }

    if (headers.find(PSEUDO_HEADER_PATH) != headers.end()) {
        request->SetPath(headers[PSEUDO_HEADER_PATH]);
        headers.erase(PSEUDO_HEADER_PATH);
    }

    if (headers.find(PSEUDO_HEADER_SCHEME) != headers.end()) {
        request->SetScheme(headers[PSEUDO_HEADER_SCHEME]);
        headers.erase(PSEUDO_HEADER_SCHEME);
    }

    if (headers.find(PSEUDO_HEADER_AUTHORITY) != headers.end()) {
        request->SetAuthority(headers[PSEUDO_HEADER_AUTHORITY]);
        headers.erase(PSEUDO_HEADER_AUTHORITY);
    }
}

void PseudoHeader::EncodeResponse(std::shared_ptr<IResponse> response) {
    // Add status pseudo-header
    response->AddHeader(PSEUDO_HEADER_STATUS, std::to_string(response->GetStatusCode()));
}

void PseudoHeader::DecodeResponse(std::shared_ptr<IResponse> response) {
    auto& headers = response->GetHeaders();

    // Extract status pseudo-header
    if (headers.find(PSEUDO_HEADER_STATUS) != headers.end()) {
        response->SetStatusCode(std::stoi(headers[PSEUDO_HEADER_STATUS]));
        headers.erase(PSEUDO_HEADER_STATUS);
    }
}

std::string PseudoHeader::MethodToString(HttpMethod method) {
    auto iter = kMethodToStringMap.find(method);
    if (iter != kMethodToStringMap.end()) {
        return iter->second;
    }

    common::LOG_FATAL("Invalid method: %d", method);
    return "";
}

HttpMethod PseudoHeader::StringToMethod(const std::string& method) {
    auto iter = kStringToMethodMap.find(method);
    if (iter != kStringToMethodMap.end()) {
        return iter->second;
    }

    common::LOG_FATAL("Invalid method: %s", method.c_str());
    return HttpMethod::kGet;
}

const std::unordered_map<std::string, HttpMethod> PseudoHeader::kStringToMethodMap = {
    {"GET", HttpMethod::kGet},
    {"POST", HttpMethod::kPost},
    {"PUT", HttpMethod::kPut},
    {"DELETE", HttpMethod::kDelete},
    {"CONNECT", HttpMethod::kConnect},
    {"OPTIONS", HttpMethod::kOptions},
    {"TRACE", HttpMethod::kTrace},
    {"PATCH", HttpMethod::kPatch},
    {"ANY", HttpMethod::kAny},
};

const std::unordered_map<HttpMethod, std::string> PseudoHeader::kMethodToStringMap = {
    {HttpMethod::kGet, "GET"},
    {HttpMethod::kPost, "POST"},
    {HttpMethod::kPut, "PUT"},
    {HttpMethod::kDelete, "DELETE"},
    {HttpMethod::kConnect, "CONNECT"},
    {HttpMethod::kOptions, "OPTIONS"},
    {HttpMethod::kTrace, "TRACE"},
    {HttpMethod::kPatch, "PATCH"},
    {HttpMethod::kAny, "ANY"},
};

}
}
