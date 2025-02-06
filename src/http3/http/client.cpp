#include "common/log/log.h"
#include "http3/http/type.h"
#include "common/http/url.h"
#include "http3/http/error.h"
#include "http3/http/client.h"
#include "common/network/address.h"
#include "common/network/io_handle.h"

namespace quicx {
namespace http3 {

std::unique_ptr<IClient> IClient::Create(const Http3Settings& settings) {
    return std::make_unique<Client>();
}

Client::Client(const Http3Settings& settings):
    settings_(settings) {
    quic_ = quic::IQuicClient::Create();
    quic_->SetConnectionStateCallBack(std::bind(&Client::OnConnection, this, 
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

Client::~Client() {
    quic_->Destroy();
    quic_->Join();
}

bool Client::Init(uint16_t thread_num, LogLevel level) {
    return quic_->Init(thread_num, quic::LogLevel(level));
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
    addr.SetPort(url_info.port);
    // create connection
    quic_->Connection(addr.GetIp(), addr.GetPort(), http3_alpn__, 10000); // TODO: timeout add to config
    wait_request_map_[addr.AsString()] = WaitRequestContext{url_info, request, handler};
    return true;
}

void Client::SetPushPromiseHandler(const http_push_promise_handler& push_promise_handler) {
    push_promise_handler_ = push_promise_handler;
}

void Client::SetPushHandler(const http_response_handler& push_handler) {
    push_handler_ = push_handler;
}

void Client::OnConnection(std::shared_ptr<quic::IQuicConnection> conn, uint32_t error, const std::string& reason) {
    if (error != 0) {
        common::LOG_ERROR("create connection failed. error: %d, reason: %s", error, reason.c_str());
        return;
    }

    std::string addr;
    uint32_t port;
    conn->GetRemoteAddr(addr, port);

    std::string host = addr + ":" + std::to_string(port);
    auto it = wait_request_map_.find(host);
    if (it == wait_request_map_.end()) {
        common::LOG_ERROR("no wait request context found. host: %s", host.c_str());
        return;
    }

    auto context = it->second;
    auto client_conn = std::make_shared<ClientConnection>(context.url.host, settings_, conn,
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

bool Client::HandlePushPromise(std::unordered_map<std::string, std::string>& headers) {
    if (!push_promise_handler_) {
        return false;
    }
    return push_promise_handler_(headers);
}

void Client::HandlePush(std::shared_ptr<IResponse> response, uint32_t error) {
    if (!push_handler_) {
        return;
    }
    push_handler_(response, error);
}

}
}
