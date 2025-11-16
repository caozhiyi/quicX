#include "common/log/log.h"
#include "http3/http/type.h"
#include "common/http/url.h"
#include "http3/http/client.h"
#include "common/network/address.h"
#include "common/network/io_handle.h"

namespace quicx {

std::unique_ptr<IClient> IClient::Create(const Http3Settings& settings) {
    return std::unique_ptr<IClient>(new http3::Client(settings));
}

namespace http3 {

Client::Client(const Http3Settings& settings):
    settings_(settings) {
    quic_ = IQuicClient::Create();
    quic_->SetConnectionStateCallBack(std::bind(&Client::OnConnection, this, 
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
}

Client::~Client() {
    quic_->Destroy();
    quic_->Join();
}

bool Client::Init(const Http3Config& config) {
    QuicClientConfig quic_config;
    quic_config.config_.worker_thread_num_ = config.thread_num_;
    quic_config.config_.log_level_ = LogLevel(config.log_level_);
    quic_config.config_.enable_ecn_ = config.enable_ecn_;
    quic_config.enable_session_cache_ = false;
    quic_config.session_cache_path_ = "";
    return quic_->Init(quic_config);
}

bool Client::DoRequest(const std::string& url, HttpMethod method,
        std::shared_ptr<IRequest> request, const http_response_handler& handler) {
    // Parse URL once for both pseudo-headers and connection management
    std::string scheme, host, path_with_query;
    uint16_t port;
    if (!common::ParseURLForPseudoHeaders(url, scheme, host, port, path_with_query)) {
        common::LOG_ERROR("parse url failed. url: %s", url.c_str());
        return false;
    }

    // Set pseudo-headers
    request->SetMethod(method);
    request->SetScheme(scheme);
    request->SetAuthority(common::BuildAuthority(host, port, scheme));
    request->SetPath(path_with_query);

    // Check if the connection is already established   
    auto it = conn_map_.find(host);
    if (it != conn_map_.end()) {
        // Use the existing connection
        auto conn = it->second;
        conn->DoRequest(request, handler);
        return true;
    }

    // Lookup address
    common::Address addr;
    if (!common::LookupAddress(host, addr)) {
        common::LOG_ERROR("lookup address failed. host: %s", host.c_str());
        return false;
    }
    addr.SetPort(port);
    
    // Create connection
    quic_->Connection(addr.GetIp(), addr.GetPort(), kHttp3Alpn, kClientConnectionTimeoutMs);
    
    wait_request_map_[addr.AsString()] = WaitRequestContext{host, request, handler};
    return true;
}

bool Client::DoRequest(const std::string& url, HttpMethod method,
        std::shared_ptr<IRequest> request, std::shared_ptr<IAsyncClientHandler> handler) {
    // Parse URL once for both pseudo-headers and connection management
    std::string scheme, host, path_with_query;
    uint16_t port;
    if (!common::ParseURLForPseudoHeaders(url, scheme, host, port, path_with_query)) {
        common::LOG_ERROR("parse url failed. url: %s", url.c_str());
        return false;
    }

    // Set pseudo-headers
    request->SetMethod(method);
    request->SetScheme(scheme);
    request->SetAuthority(common::BuildAuthority(host, port, scheme));
    request->SetPath(path_with_query);

    // Check if the connection is already established   
    auto it = conn_map_.find(host);
    if (it != conn_map_.end()) {
        // Use the existing connection
        auto conn = it->second;
        conn->DoRequest(request, handler);
        return true;
    }

    // Lookup address
    common::Address addr;
    if (!common::LookupAddress(host, addr)) {
        common::LOG_ERROR("lookup address failed. host: %s", host.c_str());
        return false;
    }
    addr.SetPort(port);
    
    // Create connection
    quic_->Connection(addr.GetIp(), addr.GetPort(), kHttp3Alpn, kClientConnectionTimeoutMs);
    
    wait_request_map_[addr.AsString()] = WaitRequestContext{host, request, handler};
    return true;
}

void Client::OnConnection(std::shared_ptr<IQuicConnection> conn, ConnectionOperation operation, uint32_t error, const std::string& reason) {
    std::string addr;
    uint32_t port;
    conn->GetRemoteAddr(addr, port);

    std::string host = addr + ":" + std::to_string(port);
    if (operation == ConnectionOperation::kConnectionClose) {
        common::LOG_INFO("connection close. error: %d, reason: %s", error, reason.c_str());
        conn_map_.erase(host);
        return;
    }

    auto it = wait_request_map_.find(host);
    if (it == wait_request_map_.end()) {
        common::LOG_ERROR("no wait request context found. host: %s", host.c_str());
        return;
    }

    auto context = it->second;
    auto client_conn = std::make_shared<ClientConnection>(context.host, settings_, conn,
        std::bind(&Client::HandleError, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&Client::HandlePushPromise, this, std::placeholders::_1),
        std::bind(&Client::HandlePush, this, std::placeholders::_1, std::placeholders::_2));

    wait_request_map_.erase(it);
    conn_map_[context.host] = client_conn;
    
    // Send request with the appropriate handler type
    if (context.IsAsync()) {
        client_conn->DoRequest(context.request, context.GetAsyncHandler());
    } else {
        client_conn->DoRequest(context.request, context.GetCompleteHandler());
    }
}

void Client::HandleError(const std::string& unique_id, uint32_t error_code) {
    common::LOG_ERROR("handle error. unique_id: %s, error_code: %d", unique_id.c_str(), error_code);
    conn_map_.erase(unique_id);
    if (error_handler_) {
        error_handler_(unique_id, error_code);
    }
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

void Client::SetPushPromiseHandler(const http_push_promise_handler& push_promise_handler) {
    push_promise_handler_ = push_promise_handler;
}

void Client::SetPushHandler(const http_response_handler& push_handler) {
    push_handler_ = push_handler;
}

void Client::SetErrorHandler(const error_handler& error_handler) {
    error_handler_ = error_handler;
}

}
}
