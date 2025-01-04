#ifndef HTTP3_HTTP_SERVER
#define HTTP3_HTTP_SERVER

#include <memory>
#include <string>
#include <unordered_map>
#include "http3/router/router.h"
#include "http3/include/if_server.h"
#include "quic/include/if_quic_server.h"
#include "http3/connection/server_connection.h"

namespace quicx {
namespace http3 {

class Server:
    public IServer {
public:
    Server();
    virtual ~Server();

    // Initialize the server with a certificate and a key
    virtual bool Init(const std::string& cert, const std::string& key,
                       uint16_t thread_num);

    // Start the server on the given address and port
    // server will block until the server is stopped
    virtual bool Start(const std::string& addr, uint16_t port);

    // Stop the server
    virtual void Stop();

    // Register a handler for a specific path
    virtual void AddHandler(HttpMethod mothed, const std::string& path, const http_handler& handler);

    // Register a middleware for a specific mothed and position
    virtual void AddMiddleware(HttpMethod mothed, MiddlewarePosition mp, const http_handler& handler);

private:
    void OnConnection(std::shared_ptr<quic::IQuicConnection> conn, uint32_t error);
    void HandleError(const std::string& unique_id, uint32_t error_code);
    void HandleRequest(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response);

private:
    std::shared_ptr<quic::IServerQuic> quic_;

    std::shared_ptr<Router> router_;
    std::unordered_map<std::string, std::shared_ptr<ServerConnection>> conn_map_;

    std::vector<http_handler> before_middlewares_;
    std::vector<http_handler> after_middlewares_;

};

}
}

#endif
