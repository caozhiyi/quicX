#ifndef HTTP3_STREAM_PSEUDO_HEADER
#define HTTP3_STREAM_PSEUDO_HEADER

#include <memory>
#include <quicx/http3/if_request.h>
#include <quicx/http3/if_response.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/util/singleton.h"

namespace quicx {
namespace http3 {

/**
 * @brief PseudoHeader is a singleton class that is used to encode and decode the pseudo-headers of the request and
 * response.
 */
class PseudoHeader: public common::Singleton<PseudoHeader> {
public:
    PseudoHeader();
    ~PseudoHeader();

    void EncodeRequest(std::shared_ptr<IRequest> request);
    // P1-H6a (RFC 9114 §4.3): validate + extract. Returns false and leaves
    // the request untouched when the field section violates the message
    // rules — missing/unknown/duplicated pseudo-headers, pseudo-headers after
    // regular fields, uppercase or connection-specific regular field names.
    // Callers must treat false as H3_MESSAGE_ERROR.
    bool DecodeRequest(std::shared_ptr<IRequest> request);

    void EncodeResponse(std::shared_ptr<IResponse> response);
    void DecodeResponse(std::shared_ptr<IResponse> response);

private:
    static std::string MethodToString(HttpMethod method);
    static HttpMethod StringToMethod(const std::string& method);
    static const std::unordered_map<std::string, HttpMethod> kStringToMethodMap;
    static const std::unordered_map<HttpMethod, std::string> kMethodToStringMap;

private:
    std::vector<std::string> request_pseudo_headers_;
    std::vector<std::string> response_pseudo_headers_;

    // Request pseudo-headers
    const std::string PSEUDO_HEADER_METHOD = ":method";
    const std::string PSEUDO_HEADER_SCHEME = ":scheme";
    const std::string PSEUDO_HEADER_AUTHORITY = ":authority";
    const std::string PSEUDO_HEADER_PATH = ":path";

    // Response pseudo-headers
    const std::string PSEUDO_HEADER_STATUS = ":status";
};

}  // namespace http3
}  // namespace quicx

#endif  // HTTP3_STREAM_PSEUDO_HEADER
