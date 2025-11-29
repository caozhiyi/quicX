#ifndef HTTP3_HTTP_CLIENT
#define HTTP3_HTTP_CLIENT

#include <memory>
#include <string>
#include <variant>
#include <unordered_map>
#include <queue>

#include "common/http/url.h"
#include "http3/include/if_client.h"
#include "quic/include/if_quic_client.h"
#include "quic/include/if_quic_connection.h"
#include "http3/connection/connection_client.h"

namespace quicx {
namespace http3 {

class Client:
    public IClient {
public:
    Client(const Http3Settings& settings = kDefaultHttp3Settings);
    virtual ~Client();

    // Initialize the client with a certificate and a key
    virtual bool Init(const Http3Config& config) override;

    // Send a request in complete mode (entire response body buffered)
    virtual bool DoRequest(const std::string& url, HttpMethod method,
        std::shared_ptr<IRequest> request, const http_response_handler& handler) override;
    
    // Send a request with async handler for streaming response
    virtual bool DoRequest(const std::string& url, HttpMethod method,
        std::shared_ptr<IRequest> request, std::shared_ptr<IAsyncClientHandler> handler) override;

    virtual void SetPushPromiseHandler(const http_push_promise_handler& push_promise_handler) override;
    virtual void SetPushHandler(const http_response_handler& push_handler) override;
    virtual void SetErrorHandler(const error_handler& error_handler) override;

private:
    void OnConnection(std::shared_ptr<IQuicConnection> conn, ConnectionOperation operation, uint32_t error, const std::string& reason);

    void HandleError(const std::string& unique_id, uint32_t error_code);
    bool HandlePushPromise(std::unordered_map<std::string, std::string>& headers);
    void HandlePush(std::shared_ptr<IResponse> response, uint32_t error);

private:
    std::shared_ptr<IQuicClient> quic_;
    std::unordered_map<std::string, std::shared_ptr<ClientConnection>> conn_map_;

    http_response_handler push_handler_;
    http_push_promise_handler push_promise_handler_;
    error_handler error_handler_;
    Http3Settings settings_;

    struct WaitRequestContext {
        std::string host;
        std::shared_ptr<IRequest> request;
        std::variant<http_response_handler, std::shared_ptr<IAsyncClientHandler>> handler;
        
        // Helper to check if async handler
        bool IsAsync() const {
            return std::holds_alternative<std::shared_ptr<IAsyncClientHandler>>(handler);
        }
        
        // Get complete mode handler
        http_response_handler GetCompleteHandler() const {
            if (std::holds_alternative<http_response_handler>(handler)) {
                return std::get<http_response_handler>(handler);
            }
            return nullptr;
        }
        
        // Get async mode handler
        std::shared_ptr<IAsyncClientHandler> GetAsyncHandler() const {
            if (std::holds_alternative<std::shared_ptr<IAsyncClientHandler>>(handler)) {
                return std::get<std::shared_ptr<IAsyncClientHandler>>(handler);
            }
            return nullptr;
        }
    };
    // Map from address string to queue of waiting requests
    // This allows multiple requests to wait for the same connection to be established
    std::unordered_map<std::string, std::queue<WaitRequestContext>> wait_request_map_;
};

}
}

#endif
