#include "common/log/log.h"
#include "http3/http/type.h"
#include "http3/http/server.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"
#include "quic/include/if_quic_server.h"
#include "http3/include/if_async_handler.h"

namespace quicx {
namespace http3 {

std::shared_ptr<IServer> IServer::Create(const Http3Settings& settings) {
    return std::make_shared<Server>(settings);
}

Server::Server(const Http3Settings& settings):
    settings_(settings) {
    quic_ = quic::IQuicServer::Create();
    router_ = std::make_shared<Router>();
    quic_->SetConnectionStateCallBack(std::bind(&Server::OnConnection, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
}

Server::~Server() {
    Stop();
}

bool Server::Init(const Http3ServerConfig& config) {
    quic::QuicServerConfig quic_config;
    if ((config.cert_pem_ == nullptr || config.key_pem_ == nullptr) &&
        (config.cert_file_.empty() || config.key_file_.empty())) {
        common::LOG_ERROR("cert file or cert pem and key file or key pem must be set.");
        return false;
    }

    if (config.cert_pem_ != nullptr && config.key_pem_ != nullptr) {
        quic_config.cert_pem_ = config.cert_pem_;
        quic_config.key_pem_ = config.key_pem_;
    } else {
        quic_config.cert_file_ = config.cert_file_;
        quic_config.key_file_ = config.key_file_;
    }
    quic_config.alpn_ = kHttp3Alpn;
    quic_config.config_.worker_thread_num_ = config.config_.thread_num_;
    quic_config.config_.log_level_ = quic::LogLevel(config.config_.log_level_);
    quic_config.config_.enable_ecn_ = config.config_.enable_ecn_;

    if (!quic_->Init(quic_config)) {
        common::LOG_ERROR("init quic server failed.");
        return false;
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

void Server::AddHandler(HttpMethod method, const std::string& path, 
                       const http_handler& handler) {
    // Create route configuration for complete mode and add to router
    RouteConfig config(handler);
    router_->AddRoute(method, path, config);
}

void Server::AddHandler(HttpMethod method, const std::string& path, 
                       std::shared_ptr<IAsyncServerHandler> handler) {
    // Create route configuration for async mode and add to router
    RouteConfig config(handler);
    router_->AddRoute(method, path, config);
}

void Server::AddMiddleware(HttpMethod mothed, MiddlewarePosition mp, const http_handler& handler) {
    if (mp == MiddlewarePosition::kBefore) {
        before_middlewares_.push_back(handler);
    } else {
        after_middlewares_.push_back(handler);
    }
}

void Server::OnConnection(std::shared_ptr<quic::IQuicConnection> conn, quic::ConnectionOperation operation, uint32_t error, const std::string& reason) {
    std::string addr;
    uint32_t port;
    conn->GetRemoteAddr(addr, port);
    std::string unique_id = addr + ":" + std::to_string(port);

    if (operation == quic::ConnectionOperation::kConnectionClose) {
        common::LOG_INFO("connection close. error: %d, reason: %s", error, reason.c_str());
        conn_map_.erase(unique_id);
        return;
    }

    // create a new server connection
    auto server_conn = std::make_shared<ServerConnection>(unique_id, settings_, 
        shared_from_this(), quic_, conn,
        std::bind(&Server::HandleError, this, std::placeholders::_1, std::placeholders::_2));

    conn_map_[unique_id] = server_conn;
}

void Server::HandleError(const std::string& unique_id, uint32_t error_code) {
    common::LOG_ERROR("handle error. unique_id: %s, error_code: %d", unique_id.c_str(), error_code);
    conn_map_.erase(unique_id);
    if (error_handler_) {
        error_handler_(unique_id, error_code);
    }
}

RouteConfig Server::MatchRoute(HttpMethod method, const std::string& path) {
    auto result = router_->Match(method, path);
    if (!result.is_match) {
        return RouteConfig(OnNotFound);
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
    response->SetBody("Not Found");
}

}
}
