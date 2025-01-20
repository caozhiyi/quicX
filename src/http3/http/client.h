#ifndef HTTP3_HTTP_CLIENT
#define HTTP3_HTTP_CLIENT

#include <memory>
#include <string>
#include <unordered_map>
#include "common/http/url.h"
#include "http3/http/request.h"
#include "quic/include/if_quic_client.h"
#include "quic/include/if_quic_connection.h"
#include "http3/connection/connection_client.h"

namespace quicx {
namespace http3 {

class Client:
    public IClient {
public:
    Client();
    virtual ~Client();

    // Initialize the client with a certificate and a key
    virtual bool Init(uint16_t thread_num = 1, LogLevel level = LL_NULL);

    // Send a request to the server
    virtual bool DoRequest(const std::string& url, HttpMethod mothed,
        std::shared_ptr<IRequest> request, const http_response_handler& handler);

    virtual void SetPushPromiseHandler(const http_push_promise_handler& push_promise_handler);
    virtual void SetPushHandler(const http_response_handler& push_handler);

private:
    void OnConnection(std::shared_ptr<quic::IQuicConnection> conn, uint32_t error, const std::string& reason);

    void HandleError(const std::string& unique_id, uint32_t error_code);
    bool HandlePushPromise(std::unordered_map<std::string, std::string>& headers);
    void HandlePush(std::shared_ptr<IResponse> response, uint32_t error);

private:
    std::shared_ptr<quic::IQuicClient> quic_;
    std::unordered_map<std::string, std::shared_ptr<ClientConnection>> conn_map_;

    struct WaitRequestContext {
        common::URL url;
        std::shared_ptr<IRequest> request;

        http_response_handler handler;
    };
    std::unordered_map<std::string, WaitRequestContext> wait_request_map_;

    http_response_handler push_handler_;
    http_push_promise_handler push_promise_handler_;
};

}
}

#endif
