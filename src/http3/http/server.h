#ifndef HTTP3_HTTP_SERVER
#define HTTP3_HTTP_SERVER

#include <memory>
#include <string>
#include <unordered_map>
#include "http3/router/router.h"
#include "http3/include/if_server.h"
#include "quic/include/if_quic_server.h"
#include "http3/connection/connection_server.h"

namespace quicx {
namespace http3 {

class Server:
    public IServer {
public:
    Server(const Http3Settings& settings = kDefaultHttp3Settings);
    virtual ~Server();

    // Initialize the server with a certificate and a key
    virtual bool Init(const std::string& cert_file, const std::string& key_file, uint16_t thread_num = 1, LogLevel level = LogLevel::kNull);
    virtual bool Init(const char* cert_pem, const char* key_pem, uint16_t thread_num = 1, LogLevel level = LogLevel::kNull);

    // Start the server on the given address and port
    // server will block until the server is stopped
    virtual bool Start(const std::string& addr, uint16_t port);

    // Stop the server
    virtual void Stop();

    virtual void Join();

    // Register a handler for a specific path
    virtual void AddHandler(HttpMethod mothed, const std::string& path, const http_handler& handler);

    // Register a middleware for a specific mothed and position
    virtual void AddMiddleware(HttpMethod mothed, MiddlewarePosition mp, const http_handler& handler);

private:
    void OnConnection(std::shared_ptr<quic::IQuicConnection> conn, quic::ConnectionOperation operation, uint32_t error, const std::string& reason);
    void HandleError(const std::string& unique_id, uint32_t error_code);
    void HandleRequest(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response);

private:
    std::shared_ptr<quic::IQuicServer> quic_;

    std::shared_ptr<Router> router_;
    std::unordered_map<std::string, std::shared_ptr<ServerConnection>> conn_map_;

    std::vector<http_handler> before_middlewares_;
    std::vector<http_handler> after_middlewares_;

    Http3Settings settings_;
};

}
}

#endif
