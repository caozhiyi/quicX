#include "common/http/url.h"
#include "common/log/log.h"
#include "common/network/address.h"
#include "common/network/io_handle.h"

#include "http3/config.h"
#include "http3/http/client.h"
#include "http3/http/error.h"

namespace quicx {

std::unique_ptr<IClient> IClient::Create(const Http3Settings& settings) {
    return std::unique_ptr<IClient>(static_cast<IClient*>(new http3::Client(settings)));
}

namespace http3 {

Client::Client(const Http3Settings& settings):
    settings_(settings) {
    quic_ = IQuicClient::Create(settings.quic_transport_params_);
    quic_->SetConnectionStateCallBack(std::bind(&Client::OnConnection, this, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
}

Client::~Client() {
    Close();
    quic_->Join();
}

bool Client::Init(const Http3ClientConfig& config) {
    // Store config for later use
    config_ = config;

    return quic_->Init(config.quic_config_);
}

bool Client::DoRequest(const std::string& url, HttpMethod method, std::shared_ptr<IRequest> request,
    const http_response_handler& handler) {
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
        // Use configured timeout (0 means no timeout, rely on idle timeout)
        uint32_t timeout_ms = config_.connection_timeout_ms_;
        quic_->Connection(addr.GetIp(), addr.GetPort(), kHttp3Alpn, timeout_ms, "", host);
    }

    return true;
}

bool Client::DoRequest(const std::string& url, HttpMethod method, std::shared_ptr<IRequest> request,
    std::shared_ptr<IAsyncClientHandler> handler) {
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
        // Use configured timeout (0 means no timeout, rely on idle timeout)
        uint32_t timeout_ms = config_.connection_timeout_ms_;
        quic_->Connection(addr.GetIp(), addr.GetPort(), kHttp3Alpn, timeout_ms, "", host);
    }

    return true;
}

void Client::OnConnection(
    std::shared_ptr<IQuicConnection> conn, ConnectionOperation operation, uint32_t error, const std::string& reason) {
    // get remote address
    std::string addr;
    uint32_t port;
    conn->GetRemoteAddr(addr, port);

    std::string addr_key = addr + ":" + std::to_string(port);

    if (operation == ConnectionOperation::kConnectionClose) {
        common::LOG_INFO("connection close. error: %d, reason: %s", error, reason.c_str());

        // Clear waiting requests for this address (connection failed)
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

    // Connection failed to create
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
        std::bind(&Client::HandlePush, this, std::placeholders::_1, std::placeholders::_2),
        config_.max_concurrent_streams_, config_.enable_push_);

    // Initialize connection (starts timers)
    client_conn->Init();

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
    // H3_NO_ERROR (0x100 = 256) indicates graceful closure, not a real error
    if (error_code == static_cast<uint32_t>(Http3ErrorCode::kNoError)) {
        common::LOG_DEBUG("handle graceful close. unique_id: %s", unique_id.c_str());
    } else {
        common::LOG_ERROR("handle error. unique_id: %s, error_code: %d", unique_id.c_str(), error_code);
    }
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

void Client::Close() {
    common::LOG_INFO("Client::Close() - gracefully closing all connections (%zu active)", conn_map_.size());

    // Close all active HTTP/3 connections
    for (auto& pair : conn_map_) {
        common::LOG_DEBUG("Closing connection to %s", pair.first.c_str());
        pair.second->Close(0);  // error_code=0 means normal close
    }

    quic_->AddTimer(kConnectionCloseDestroyTimeoutMs, [this]() { quic_->Destroy(); });
}

bool Client::InitiateMigration() {
    bool result = false;
    common::LOG_INFO("Client::InitiateMigration() - initiating migration on %zu connections", conn_map_.size());

    // Initiate migration on all active HTTP/3 connections
    for (auto& pair : conn_map_) {
        common::LOG_DEBUG("Initiating migration on connection to %s", pair.first.c_str());
        if (pair.second->InitiateMigration()) {
            result = true;
            common::LOG_INFO("Migration initiated on connection to %s", pair.first.c_str());
        } else {
            common::LOG_WARN("Migration failed on connection to %s", pair.first.c_str());
        }
    }

    return result;
}

MigrationResult Client::InitiateMigrationTo(const std::string& local_ip, uint16_t local_port) {
    common::LOG_INFO("Client::InitiateMigrationTo() - initiating migration to %s:%d on %zu connections",
        local_ip.c_str(), local_port, conn_map_.size());

    if (conn_map_.empty()) {
        common::LOG_WARN("Client::InitiateMigrationTo: no active connections");
        return MigrationResult::kFailedInvalidState;
    }

    // Initiate migration on the first (or all) active HTTP/3 connection(s)
    // For simplicity, we initiate on the first connection. In production,
    // you might want to migrate all connections or a specific one.
    for (auto& pair : conn_map_) {
        common::LOG_DEBUG(
            "Initiating migration to %s:%d on connection to %s", local_ip.c_str(), local_port, pair.first.c_str());
        auto result = pair.second->InitiateMigrationTo(local_ip, local_port);
        if (result == MigrationResult::kSuccess) {
            common::LOG_INFO("Migration initiated on connection to %s", pair.first.c_str());
            return result;
        } else {
            common::LOG_WARN(
                "Migration failed on connection to %s with result %d", pair.first.c_str(), static_cast<int>(result));
        }
    }

    return MigrationResult::kFailedInvalidState;
}

void Client::SetMigrationCallback(migration_callback cb) {
    migration_cb_ = cb;

    // Forward callback to all existing connections
    for (auto& pair : conn_map_) {
        pair.second->SetMigrationCallback(cb);
    }
}

}  // namespace http3
}  // namespace quicx
