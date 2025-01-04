#ifndef HTTP3_STREAM_PSEUDO_HEADER
#define HTTP3_STREAM_PSEUDO_HEADER

#include <string>
#include <vector>
#include "common/util/singleton.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"

namespace quicx {
namespace http3 {

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
    std::string MethodToString(HttpMothed method);
    HttpMothed StringToMethod(const std::string& method);

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
