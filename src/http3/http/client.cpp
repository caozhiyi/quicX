#include "common/log/log.h"
#include "http3/http/type.h"
#include "http3/http/error.h"
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
    
    std::string addr_key = addr.AsString();
    
    // Add request to waiting queue
    wait_request_map_[addr_key].push(WaitRequestContext{host, request, handler});
    
    // If this is the first request for this address, create connection
    if (wait_request_map_[addr_key].size() == 1) {
        quic_->Connection(addr.GetIp(), addr.GetPort(), kHttp3Alpn, kClientConnectionTimeoutMs, "", host);
    }
    
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
    
    std::string addr_key = addr.AsString();
    
    // Add request to waiting queue
    wait_request_map_[addr_key].push(WaitRequestContext{host, request, handler});
    
    // If this is the first request for this address, create connection
    if (wait_request_map_[addr_key].size() == 1) {
        quic_->Connection(addr.GetIp(), addr.GetPort(), kHttp3Alpn, kClientConnectionTimeoutMs, "", host);
    }
    
    return true;
}

void Client::OnConnection(std::shared_ptr<IQuicConnection> conn, ConnectionOperation operation, uint32_t error, const std::string& reason) {
    std::string addr;
    uint32_t port;
    conn->GetRemoteAddr(addr, port);

    std::string addr_key = addr + ":" + std::to_string(port);
    
    if (operation == ConnectionOperation::kConnectionClose) {
        common::LOG_INFO("connection close. error: %d, reason: %s", error, reason.c_str());
        
        // Clear waiting requests for this address (connection failed)
        // Note: The connection will be removed from conn_map_ in HandleError callback
        auto wait_it = wait_request_map_.find(addr_key);
        if (wait_it != wait_request_map_.end()) {
            // Notify all waiting requests about the connection failure
            while (!wait_it->second.empty()) {
                auto& context = wait_it->second.front();
                // Call error handler if available
                if (error_handler_) {
                    error_handler_(context.host, error != 0 ? error : Http3ErrorCode::kInternalError);
                }
                wait_it->second.pop();
            }
            wait_request_map_.erase(wait_it);
        }
        return;
    }

    // Connection created - check if it succeeded
    if (error != 0) {
        common::LOG_ERROR("connection creation failed. error: %d, reason: %s", error, reason.c_str());
        // Handle connection failure - notify all waiting requests
        auto wait_it = wait_request_map_.find(addr_key);
        if (wait_it != wait_request_map_.end()) {
            while (!wait_it->second.empty()) {
                auto& context = wait_it->second.front();
                // Call error handler if available
                if (error_handler_) {
                    error_handler_(context.host, error);
                }
                wait_it->second.pop();
            }
            wait_request_map_.erase(wait_it);
        }
        return;
    }

    // Connection established successfully
    auto wait_it = wait_request_map_.find(addr_key);
    if (wait_it == wait_request_map_.end() || wait_it->second.empty()) {
        common::LOG_ERROR("no wait request context found for connection. addr: %s", addr_key.c_str());
        return;
    }

    // Get the first context to determine the host name for connection mapping
    auto first_context = wait_it->second.front();
    std::string host_name = first_context.host;

    // Create client connection
    auto client_conn = std::make_shared<ClientConnection>(host_name, settings_, conn,
        std::bind(&Client::HandleError, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&Client::HandlePushPromise, this, std::placeholders::_1),
        std::bind(&Client::HandlePush, this, std::placeholders::_1, std::placeholders::_2));

    // Store connection in map
    conn_map_[host_name] = client_conn;

    // Process all waiting requests in the queue
    while (!wait_it->second.empty()) {
        auto& context = wait_it->second.front();
        
        // Send request with the appropriate handler type
        if (context.IsAsync()) {
            client_conn->DoRequest(context.request, context.GetAsyncHandler());
        } else {
            client_conn->DoRequest(context.request, context.GetCompleteHandler());
        }
        
        wait_it->second.pop();
    }

    // Remove the empty queue from wait_request_map_
    wait_request_map_.erase(wait_it);
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
