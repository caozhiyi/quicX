#ifndef HTTP3_STREAM_PSEUDO_HEADER
#define HTTP3_STREAM_PSEUDO_HEADER

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include "common/util/singleton.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"

namespace quicx {
namespace http3 {

/**
 * @brief PseudoHeader is a singleton class that is used to encode and decode the pseudo-headers of the request and response.
 */
class PseudoHeader:
    public common::Singleton<PseudoHeader> {
public:
    PseudoHeader();
    ~PseudoHeader();

    void EncodeRequest(std::shared_ptr<IRequest> request);
    void DecodeRequest(std::shared_ptr<IRequest> request);

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

}
}

#endif
