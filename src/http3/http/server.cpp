#include "common/log/log.h"
#include "http3/http/server.h"
#include "common/network/address.h"
#include "quic/include/if_quic_server.h"

namespace quicx {
namespace http3 {

std::unique_ptr<IServer> IServer::Create() {
    return std::make_unique<Server>();
}

Server::Server() {
    quic_ = quic::IQuicServer::Create();
    router_ = std::make_shared<Router>();
}

Server::~Server() {
    Stop();
}

bool Server::Init(const std::string& cert, const std::string& key, uint16_t thread_num) {
    if (!quic_->Init(cert, key, thread_num)) {
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

void Server::AddHandler(HttpMethod mothed, const std::string& path, const http_handler& handler) {
    router_->AddRoute(mothed, path, handler);
}

void Server::AddMiddleware(HttpMethod mothed, MiddlewarePosition mp, const http_handler& handler) {
    if (mp == MiddlewarePosition::MP_BEFORE) {
        before_middlewares_.push_back(handler);
    } else {
        after_middlewares_.push_back(handler);
    }
}

void Server::OnConnection(std::shared_ptr<quic::IQuicConnection> conn, uint32_t error) {
    if (error != 0) {
        common::LOG_ERROR("create connection failed. error: %d", error);
        return;
    }

    std::string addr;
    uint32_t port;
    conn->GetRemoteAddr(addr, port);
    std::string unique_id = addr + ":" + std::to_string(port);

    auto server_conn = std::make_shared<ServerConnection>(unique_id, conn,
        std::bind(&Server::HandleError, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&Server::HandleRequest, this, std::placeholders::_1, std::placeholders::_2));

    conn_map_[unique_id] = server_conn;
}

void Server::HandleError(const std::string& unique_id, uint32_t error_code) {
    common::LOG_ERROR("handle error. unique_id: %s, error_code: %d", unique_id.c_str(), error_code);
    conn_map_.erase(unique_id);
}

void Server::HandleRequest(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) {
    std::string path = request->GetPath();
    HttpMethod mothed = request->GetMethod();

    auto match_result = router_->Match(mothed, path);
    if (!match_result.is_match) {
        response->SetStatusCode(404);
        response->SetBody("Not Found");
        return;
    }

    for (auto& handler : before_middlewares_) {
        handler(request, response);
    }

    match_result.handler(request, response);

    for (auto& handler : after_middlewares_) {
        handler(request, response);
    }
}

}
}
