#include "http3/http/client.h"

namespace quicx {
namespace http3 {

Client::Client() {
    quic_ = nullptr;
    _conn = nullptr;
}

Client::~Client() {

}

bool Client::Init(uint16_t thread_num) {
    return true;
}

bool Client::DoRequest(const std::string& url, const IRequest& request, const http_response_handler& handler) {
    return true;
}

}
}
