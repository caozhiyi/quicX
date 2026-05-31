#ifndef HTTP3_HTTP_SERVER
#define HTTP3_HTTP_SERVER

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "http3/connection/connection_server.h"
#include <quicx/http3/if_server.h>
#include "http3/router/router.h"
#include "http3/stream/response_stream.h"
#include <quicx/quic/if_quic_server.h>


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

    // Register a middleware for a specific method and position
    virtual void AddMiddleware(HttpMethod method, MiddlewarePosition mp, const http_handler& handler) override;

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
    virtual void BeforeHandlerProcess(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) override;
    // After handler process
    virtual void AfterHandlerProcess(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) override;

    // Not found handler
    static void OnNotFound(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response);

private:
    std::shared_ptr<IQuicServer> quic_;

    std::shared_ptr<Router> router_;
    // conn_map_ is keyed by the underlying IQuicConnection* (raw pointer
    // identity), NOT by the human-readable "addr:port" unique_id. The
    // unique_id form is unsafe under benchmark workloads where many
    // concurrent QUIC connections legitimately share the same addr:port
    // tuple (loopback ephemeral-port reuse, NAT, multi-stream client) and
    // would either silently overwrite live entries on insert or erase the
    // wrong, healthy connection on close — which manifested as a 5-10s
    // stall + cascading "connection close. error: 0, reason:" storm in
    // long perf runs (see analysis 2026-05-29). The unique_id string is
    // still kept on each ServerConnection (GetUniqueId()) and used by
    // HandleError() / the user-facing error_handler_ for human-readable
    // reporting.
    //
    // Thread-safety: in multi-thread mode QuicServer dispatches connection
    // state callbacks from per-worker event-loop threads. OnConnection is
    // therefore invoked from many worker threads concurrently for distinct
    // connections, all touching this same map (insert on accept, erase on
    // close). HandleError walks the map from yet another thread context.
    // TSan confirmed all three call sites racing on the underlying
    // _Hashtable. We serialise every access through conn_map_mu_; the
    // critical sections are short (a hash insert/erase/find) so a plain
    // mutex is plenty — there is no high-frequency read path here that
    // would benefit from a shared_mutex like the client's hot lookup.
    mutable std::mutex conn_map_mu_;
    std::unordered_map<void*, std::shared_ptr<ServerConnection>> conn_map_;

    std::vector<http_handler> before_middlewares_;
    std::vector<http_handler> after_middlewares_;
    error_handler error_handler_;

    Http3Settings settings_;
    Http3ServerConfig config_;  // Store config for local connection limits
};

}  // namespace http3
}  // namespace quicx

#endif
