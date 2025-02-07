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
    switch (method) {
        case HttpMethod::HM_GET:     return "GET";
        case HttpMethod::HM_POST:    return "POST";
        case HttpMethod::HM_PUT:     return "PUT";
        case HttpMethod::HM_DELETE:  return "DELETE";
        case HttpMethod::HM_CONNECT: return "CONNECT";
        case HttpMethod::HM_OPTIONS: return "OPTIONS";
        case HttpMethod::HM_TRACE:   return "TRACE";
        case HttpMethod::HM_PATCH:   return "PATCH";
        case HttpMethod::HM_ANY:     return "ANY";
        default:
        common::LOG_FATAL("Invalid method: %d", method);
        return "GET";
    }
}

HttpMethod PseudoHeader::StringToMethod(const std::string& method) {
    if (method == "GET")     return HttpMethod::HM_GET;
    if (method == "POST")    return HttpMethod::HM_POST;
    if (method == "PUT")     return HttpMethod::HM_PUT;
    if (method == "DELETE")  return HttpMethod::HM_DELETE;
    if (method == "CONNECT") return HttpMethod::HM_CONNECT;
    if (method == "OPTIONS") return HttpMethod::HM_OPTIONS;
    if (method == "TRACE")   return HttpMethod::HM_TRACE;
    if (method == "PATCH")   return HttpMethod::HM_PATCH;
    if (method == "ANY")     return HttpMethod::HM_ANY;

    common::LOG_FATAL("Invalid method: %s", method.c_str());
    return HttpMethod::HM_GET;
}

}
}
