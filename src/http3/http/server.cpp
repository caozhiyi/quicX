#include "common/log/log.h"
#include <quicx/common/metrics.h>

#include <quicx/quic/if_quic_server.h>

#include "http3/config.h"
#include "http3/http/server.h"
#include <quicx/http3/if_async_handler.h>
#include <quicx/http3/if_request.h>
#include <quicx/http3/if_response.h>
#include "http3/metric/metrics_handler.h"

namespace quicx {

std::shared_ptr<IServer> IServer::Create(const Http3Settings& settings) {
    return std::make_shared<http3::Server>(settings);
}
namespace http3 {

Server::Server(const Http3Settings& settings):
    settings_(settings) {
    quic_ = IQuicServer::Create(settings.quic_transport_params_);
    router_ = std::make_shared<Router>();
    quic_->SetConnectionStateCallBack([this](auto a, auto b, auto c, auto d) {
        OnConnection(a, b, c, d);
    });
}

Server::~Server() {
    // Synchronously wait for the underlying quic server's master thread
    // to finish before dropping any http-level state. Otherwise the master
    // thread may still be executing an event-loop callback (e.g. a close/
    // draining timer fired for a server connection) that transitively
    // touches conn_map_ / router_ / ServerConnection objects while we are
    // already in the middle of destroying them.
    Stop();
    Join();
    // With the master thread joined we know no more callbacks will run on
    // our ServerConnection entries. Explicitly drop them so the
    // shared_ptr<IQuicConnection> chain collapses here, before ~QuicServer
    // runs and releases the last strong references on the worker side.
    {
        std::lock_guard<std::mutex> lock(conn_map_mu_);
        conn_map_.clear();
    }
}

bool Server::Init(const Http3ServerConfig& config) {
    // Store config for later use
    config_ = config;

    // Basic validation (though QuicServer might handle it)
    if ((config.quic_config_.cert_pem_ == nullptr || config.quic_config_.key_pem_ == nullptr) &&
        (config.quic_config_.cert_file_.empty() || config.quic_config_.key_file_.empty())) {
        LOG_ERROR("cert file or cert pem and key file or key pem must be set.");
        return false;
    }

    // Copy the config and enforce ALPN
    QuicServerConfig quic_config = config.quic_config_;
    quic_config.alpn_ = kHttp3Alpn;

    if (!quic_->Init(quic_config)) {
        LOG_ERROR("init quic server failed.");
        return false;
    }

    // Initialize global metrics
    common::Metrics::Initialize(config.metrics_);

    // Auto-register metrics endpoint if enabled
    if (config.metrics_.http_enable) {
        AddHandler(HttpMethod::kGet, config.metrics_.http_path, MetricsHandler::Handle);
        LOG_INFO("Metrics endpoint registered at %s", config.metrics_.http_path.c_str());
    }

    return true;
}

bool Server::Start(const std::string& addr, uint16_t port) {
    return quic_->ListenAndAccept(addr, port);
}

void Server::Stop() {
    quic_->Destroy();
}

void Server::Join() {
    quic_->Join();
}

void Server::AddHandler(HttpMethod method, const std::string& path, const http_handler& handler) {
    // Create route configuration for complete mode and add to router
    RouteConfig config(handler);
    router_->AddRoute(method, path, config);
}

void Server::AddHandler(HttpMethod method, const std::string& path, std::shared_ptr<IAsyncServerHandler> handler) {
    // Create route configuration for async mode and add to router
    RouteConfig config(handler);
    router_->AddRoute(method, path, config);
}

void Server::AddMiddleware(HttpMethod method, MiddlewarePosition mp, const http_handler& handler) {
    if (mp == MiddlewarePosition::kBefore) {
        before_middlewares_.push_back(handler);
    } else {
        after_middlewares_.push_back(handler);
    }
}

void Server::OnConnection(
    std::shared_ptr<IQuicConnection> conn, ConnectionOperation operation, uint32_t error, const std::string& reason) {
    std::string addr;
    uint32_t port;
    conn->GetRemoteAddr(addr, port);
    // unique_id is purely a human-readable label (used by HandleError logging
    // and the user-facing error_handler_); it MUST NOT be used as the
    // conn_map_ key because under benchmark workloads many concurrent QUIC
    // connections legitimately share the same (peer_addr:peer_port) tuple
    // (loopback ephemeral-port reuse, NAT, multi-stream client). Keying by
    // the address-string caused two cascading-close bugs in long perf runs
    // (see analysis 2026-05-29):
    //   * Insert path: a fresh handshake with the same addr/port silently
    //     dropped the previous shared_ptr<ServerConnection>.
    //   * Erase path: a watchdog-fired handshake-timeout close erased the
    //     wrong, perfectly healthy connection that happened to share the
    //     same addr/port string, instantly tearing down all in-flight
    //     business connections on that worker — observable as the 5-10s
    //     stall + "connection close. error: 0, reason:" storm.
    // We now key conn_map_ by the underlying IQuicConnection raw pointer,
    // which is process-uniquely identifying for the lifetime of the QUIC
    // connection. This mirrors the reverse-lookup-by-pointer fix already in
    // http3::Client (see client.cpp:230-236).
    std::string unique_id = addr + ":" + std::to_string(port);
    void* conn_key = conn.get();

    if (operation == ConnectionOperation::kConnectionClose) {
        LOG_INFO("connection close. error: %d, reason: %s", error, reason.c_str());
        {
            std::lock_guard<std::mutex> lock(conn_map_mu_);
            conn_map_.erase(conn_key);
        }
        return;
    }

    // create a new server connection
    auto server_conn = std::make_shared<ServerConnection>(unique_id, settings_, shared_from_this(), quic_, conn,
        [this](auto a, auto b) { HandleError(a, b); },
        config_.max_concurrent_streams_, config_.enable_push_);

    // Initialize connection (starts timers)
    server_conn->Init();

    {
        std::lock_guard<std::mutex> lock(conn_map_mu_);
        conn_map_[conn_key] = server_conn;
    }
}

void Server::HandleError(const std::string& unique_id, uint32_t error_code) {
    LOG_ERROR("handle error. unique_id: %s, error_code: %d", unique_id.c_str(), error_code);
    // Reverse-lookup: HandleError still uses the human-readable unique_id
    // string so the user-supplied error_handler_ keeps a stable identity,
    // but conn_map_ is now keyed by IQuicConnection pointer (see
    // OnConnection above for why). Walk the map and erase by matching the
    // ServerConnection's stored unique_id. We hold the lock for the whole
    // walk so a concurrent OnConnection insert/erase from another worker
    // thread cannot invalidate our iterator mid-traversal.
    {
        std::lock_guard<std::mutex> lock(conn_map_mu_);
        for (auto it = conn_map_.begin(); it != conn_map_.end(); ++it) {
            if (it->second && it->second->GetUniqueId() == unique_id) {
                conn_map_.erase(it);
                break;
            }
        }
    }
    if (error_handler_) {
        error_handler_(unique_id, error_code);
    }
}

RouteConfig Server::MatchRoute(HttpMethod method, const std::string& path, std::shared_ptr<IRequest> request) {
    auto result = router_->Match(method, path);
    if (!result.is_match) {
        return RouteConfig(OnNotFound);
    }
    // Set path parameters to request if provided
    if (request && !result.params.empty()) {
        request->SetPathParams(result.params);
    }
    return result.config;
}

void Server::BeforeHandlerProcess(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) {
    for (auto& middleware : before_middlewares_) {
        middleware(request, response);
    }
}

void Server::AfterHandlerProcess(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) {
    for (auto& middleware : after_middlewares_) {
        middleware(request, response);
    }
}

void Server::OnNotFound(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) {
    response->SetStatusCode(404);
    response->AppendBody(std::string("Not Found"));
}

}  // namespace http3
}  // namespace quicx
