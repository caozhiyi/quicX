#include "common/log/log.h"
#include "common/http/url.h"
#include "http3/http/client.h"
#include "common/network/address.h"
#include "common/network/io_handle.h"

namespace quicx {
namespace http3 {

std::unique_ptr<IClient> IClient::Create() {
    return std::make_unique<Client>();
}

Client::Client() {
    quic_ = nullptr;
}

Client::~Client() {

}

bool Client::Init(uint16_t thread_num) {
    return true;
}

bool Client::DoRequest(const std::string& url, HttpMethod mothed,
        std::shared_ptr<IRequest> request, const http_response_handler& handler) {
    // parse url
    common::URL url_info;
    if (!common::ParseURL(url, url_info)) {
        common::LOG_ERROR("parse url failed. url: %s", url.c_str());
        return false;
    }

    request->SetPath(url_info.path);
    request->SetMethod(mothed);
    request->SetScheme(url_info.scheme);
    request->SetAuthority(url_info.host);

    // check if the connection is already established   
    auto it = conn_map_.find(url_info.host);
    if (it != conn_map_.end()) {
        // use the existing connection
        auto conn = it->second;
        conn->DoRequest(request, handler);
        return true;
    }

    // lookup address
    common::Address addr;
    if (!common::LookupAddress(url_info.host, addr)) {
        common::LOG_ERROR("lookup address failed. host: %s", url_info.host.c_str());
        return false;
    }

    // create connection
    quic_->Connection(addr.GetIp(), addr.GetPort(), 10000); // TODO: timeout add to config
    wait_request_map_[addr.AsString()] = WaitRequestContext{url_info, request, handler};
    return true;
}

void Client::OnConnection(std::shared_ptr<quic::IQuicConnection> conn, uint32_t error) {
    if (error != 0) {
        common::LOG_ERROR("create connection failed. error: %d", error);
        return;
    }

    std::string addr;
    uint32_t port;
    conn->GetRemoteAddr(addr, port);

    std::string host = addr + ":" + std::to_string(port);
    auto it = wait_request_map_.find(addr);
    if (it == wait_request_map_.end()) {
        common::LOG_ERROR("no wait request context found. addr: %s", addr.c_str());
        return;
    }

    auto context = it->second;
    auto client_conn = std::make_shared<ClientConnection>(context.url.host, conn,
        std::bind(&Client::HandleError, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&Client::HandlePushPromise, this, std::placeholders::_1),
        std::bind(&Client::HandlePush, this, std::placeholders::_1, std::placeholders::_2));

    wait_request_map_.erase(it);
    conn_map_[context.url.host] = client_conn;
    client_conn->DoRequest(context.request, context.handler);
}

void Client::HandleError(const std::string& unique_id, uint32_t error_code) {
    common::LOG_ERROR("handle error. unique_id: %s, error_code: %d", unique_id.c_str(), error_code);
}

void Client::HandlePushPromise(std::unordered_map<std::string, std::string>& headers) {

}

void Client::HandlePush(std::shared_ptr<IResponse> response, uint32_t error) {

}

}
}
