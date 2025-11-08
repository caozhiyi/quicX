#ifndef HTTP3_HTTP_SERVER
#define HTTP3_HTTP_SERVER

#include <memory>
#include <string>
#include <unordered_map>

#include "http3/router/router.h"
#include "http3/include/if_server.h"
#include "quic/include/if_quic_server.h"
#include "http3/stream/response_stream.h"
#include "http3/connection/connection_server.h"

namespace quicx {
namespace http3 {

class Server:
    public IServer,
    public IHttpProcessor,
    public std::enable_shared_from_this<Server> {
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
    virtual void AddHandler(HttpMethod method, const std::string& path, 
                           const http_handler& handler) override;
    
    // Register an async handler for streaming mode
    virtual void AddHandler(HttpMethod method, const std::string& path, 
                           std::shared_ptr<IAsyncServerHandler> handler) override;

    // Register a middleware for a specific method and position
    virtual void AddMiddleware(HttpMethod method, MiddlewarePosition mp, const http_handler& handler) override;

    // Set the error handler
    virtual void SetErrorHandler(const error_handler& error_handler) override {
        error_handler_ = error_handler;
    }

private:
    void OnConnection(std::shared_ptr<quic::IQuicConnection> conn, quic::ConnectionOperation operation, uint32_t error, const std::string& reason);
    void HandleError(const std::string& unique_id, uint32_t error_code);
    // Match route and return route configuration
    virtual RouteConfig MatchRoute(HttpMethod method, const std::string& path) override;
    // Before handler process
    virtual void BeforeHandlerProcess(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) override;
    // After handler process
    virtual void AfterHandlerProcess(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) override;

    // Not found handler
    static void OnNotFound(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response);

private:
    std::shared_ptr<quic::IQuicServer> quic_;

    std::shared_ptr<Router> router_;
    std::unordered_map<std::string, std::shared_ptr<ServerConnection>> conn_map_;

    std::vector<http_handler> before_middlewares_;
    std::vector<http_handler> after_middlewares_;
    error_handler error_handler_;

    Http3Settings settings_;
};

}
}

#endif
