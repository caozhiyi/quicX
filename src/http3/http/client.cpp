#include "http3/http/client.h"

namespace quicx {
namespace http3 {

Client::Client() {
    quic_ = nullptr;
    _conn = nullptr;
}

Client::~Client() {
    if (_conn) {
        _conn->Close();
        _conn = nullptr;
    }
    if (quic_) {
        quic_->Stop();
        quic_ = nullptr;
    }
}

bool Client::Init(const std::string& cert, const std::string& key, uint16_t thread_num) {
    quic_ = std::make_shared<quic::Quic>();
    if (!quic_->Init(cert, key, thread_num)) {
        return false;
    }
    return true;
}

bool Client::Start() {
    if (!quic_) {
        return false;
    }
    return quic_->Start();
}

bool Client::Stop() {
    if (!quic_) {
        return false;
    }
    quic_->Stop();
    return true;
}

bool Client::DoRequest(const IRequest& request, const http_handler handler) {
    if (!quic_) {
        return false;
    }

    // Create connection if not exists
    if (!_conn) {
        _conn = quic_->CreateConnection();
        if (!_conn) {
            return false;
        }
    }

    // TODO: Implement request sending logic
    // Need to:
    // 1. Create HTTP3 stream
    // 2. Encode request into HTTP3 frames
    // 3. Send frames through QUIC stream
    // 4. Handle response asynchronously via handler callback

    return true;
}

}
}
