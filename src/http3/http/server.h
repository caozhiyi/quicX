#ifndef HTTP3_HTTP_SERVER
#define HTTP3_HTTP_SERVER


#include <memory>
#include <quicx/http3/if_server.h>
#include <quicx/quic/if_quic_server.h>
#include <string>
#include <vector>

#include "http3/connection/connection_server.h"
#include "http3/http/middleware_chain.h"
#include "http3/router/router.h"
#include "http3/stream/response_stream.h"

namespace quicx {
namespace http3 {

class Server: public IServer, public IHttpProcessor, public std::enable_shared_from_this<Server> {
public:
    Server(const Http3Settings& settings = kDefaultHttp3Settings);
    virtual ~Server();

    // Initialize the server with a certificate and a key
    virtual bool Init(const Http3ServerConfig& config) override;

    // Start the server on the given address and port
    // server will block until the server is stopped
    virtual bool Start(const std::string& addr, uint16_t port) override;

    // Stop the server
    virtual void Stop() override;

    virtual void Join() override;

    // Register a handler for complete mode (entire body buffered)
    virtual void AddHandler(HttpMethod method, const std::string& path, const http_handler& handler) override;

    // Register an async handler for streaming mode
    virtual void AddHandler(
        HttpMethod method, const std::string& path, std::shared_ptr<IAsyncServerHandler> handler) override;

    // Register a middleware that can short-circuit the pipeline (P1-H8).
    // The legacy http_handler overload is inherited from IServer (default
    // implementation wraps it into a never-short-circuiting middleware_handler).
    virtual void AddMiddleware(HttpMethod method, MiddlewarePosition mp, const middleware_handler& handler) override;

    // Set the error handler
    virtual void SetErrorHandler(const error_handler& error_handler) override { error_handler_ = error_handler; }

private:
    void OnConnection(std::shared_ptr<IQuicConnection> conn, ConnectionOperation operation, uint32_t error,
        const std::string& reason);
    void HandleError(const std::string& unique_id, uint32_t error_code);
    // Match route and return route configuration
    virtual RouteConfig MatchRoute(
        HttpMethod method, const std::string& path, std::shared_ptr<IRequest> request = nullptr) override;
    // Before handler process
    // P1-H8: before-middlewares can short-circuit the pipeline. Returns false
    // when a middleware did so — the route handler and after-middlewares must
    // be skipped and the middleware-produced response flushed.
    virtual bool BeforeHandlerProcess(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) override;
    // After handler process
    virtual void AfterHandlerProcess(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) override;
    // P1-H6g: per-instance request body cap (from Http3ServerConfig).
    virtual uint64_t GetMaxRequestBodySize() const override { return config_.max_request_body_size_; }

    // Not found handler
    static void OnNotFound(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response);

private:
    std::shared_ptr<IQuicServer> quic_;

    std::shared_ptr<Router> router_;

    // SERVER-SIDE OWNERSHIP
    // ---------------------
    // A ServerConnection's lifetime is owned directly by its QUIC connection:
    // Server::OnConnection binds it via IQuicConnection::SetContext(
    // shared_ptr<ServerConnection>) on connect, and the QUIC connection holds
    // that shared_ptr until the connection object is destroyed. No central
    // per-server map is needed, so there is no shared hashtable to race on
    // (the old conn_map_ + recursive_mutex caused TSan races and a re-entrant
    // self-deadlock) and no deferred-release vector (the old closing_conns_,
    // added to dodge a synchronous re-entrant ~ServerConnection UAF). The only
    // back-reference from ServerConnection to its IQuicConnection is a
    // std::weak_ptr, which breaks the reference cycle the old design patched
    // around.

    // P1-H8: method-aware middleware pipeline with short-circuit support
    // (replaces the flat before_/after_ vectors that ignored the method filter).
    MiddlewareChain middlewares_;
    error_handler error_handler_;

    Http3Settings settings_;
    Http3ServerConfig config_;  // Store config for local connection limits
};

}  // namespace http3
}  // namespace quicx

#endif  // HTTP3_HTTP_SERVER

