#include "common/log/log.h"
#include "common/metrics/metrics.h"

#include "quic/include/if_quic_server.h"

#include "http3/config.h"
#include "http3/http/server.h"
#include "http3/include/if_async_handler.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"
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
    quic_->SetConnectionStateCallBack(std::bind(&Server::OnConnection, this, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
}

Server::~Server() {
    Stop();
}

bool Server::Init(const Http3ServerConfig& config) {
    // Store config for later use
    config_ = config;

    // Basic validation (though QuicServer might handle it)
    if ((config.quic_config_.cert_pem_ == nullptr || config.quic_config_.key_pem_ == nullptr) &&
        (config.quic_config_.cert_file_.empty() || config.quic_config_.key_file_.empty())) {
        common::LOG_ERROR("cert file or cert pem and key file or key pem must be set.");
        return false;
    }

    // Copy the config and enforce ALPN
    QuicServerConfig quic_config = config.quic_config_;
    quic_config.alpn_ = kHttp3Alpn;

    if (!quic_->Init(quic_config)) {
        common::LOG_ERROR("init quic server failed.");
        return false;
    }

    // Initialize global metrics
    common::Metrics::Initialize(config.metrics_);

    // Auto-register metrics endpoint if enabled
    if (config.metrics_.http_enable) {
        AddHandler(HttpMethod::kGet, config.metrics_.http_path, MetricsHandler::Handle);
        common::LOG_INFO("Metrics endpoint registered at %s", config.metrics_.http_path.c_str());
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
    std::string unique_id = addr + ":" + std::to_string(port);

    if (operation == ConnectionOperation::kConnectionClose) {
        common::LOG_INFO("connection close. error: %d, reason: %s", error, reason.c_str());
        conn_map_.erase(unique_id);
        return;
    }

    // create a new server connection
    auto server_conn = std::make_shared<ServerConnection>(unique_id, settings_, shared_from_this(), quic_, conn,
        std::bind(&Server::HandleError, this, std::placeholders::_1, std::placeholders::_2),
        config_.max_concurrent_streams_, config_.enable_push_);

    // Initialize connection (starts timers)
    server_conn->Init();

    conn_map_[unique_id] = server_conn;
}

void Server::HandleError(const std::string& unique_id, uint32_t error_code) {
    common::LOG_ERROR("handle error. unique_id: %s, error_code: %d", unique_id.c_str(), error_code);
    conn_map_.erase(unique_id);
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
