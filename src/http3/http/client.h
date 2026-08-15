#ifndef HTTP3_HTTP_CLIENT
#define HTTP3_HTTP_CLIENT

#include <memory>
#include <queue>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <quicx/http3/if_client.h>
#include <quicx/quic/if_quic_client.h>
#include <quicx/quic/if_quic_connection.h>
#include "http3/connection/connection_client.h"

namespace quicx {
namespace http3 {

class Client: public IClient {
public:
    Client(const Http3Settings& settings = kDefaultHttp3Settings);
    virtual ~Client();

    // Initialize the client with a certificate and a key
    // Initialize the client with config
    virtual bool Init(const Http3ClientConfig& config) override;

    // Send a request in complete mode (entire response body buffered)
    virtual bool DoRequest(const std::string& url, HttpMethod method, std::shared_ptr<IRequest> request,
        const http_response_handler& handler) override;

    // Send a request with async handler for streaming response
    virtual bool DoRequest(const std::string& url, HttpMethod method, std::shared_ptr<IRequest> request,
        std::shared_ptr<IAsyncClientHandler> handler) override;

    virtual void SetPushPromiseHandler(const http_push_promise_handler& push_promise_handler) override;
    virtual void SetPushHandler(const http_response_handler& push_handler) override;
    virtual void SetErrorHandler(const error_handler& error_handler) override;

    virtual void Close() override;

    virtual bool InitiateMigration() override;

    virtual MigrationResult InitiateMigrationTo(const std::string& local_ip, uint16_t local_port = 0) override;

    virtual void SetMigrationCallback(migration_callback cb) override;

private:
    void OnConnection(std::shared_ptr<IQuicConnection> conn, ConnectionOperation operation, uint32_t error,
        const std::string& reason);

    void HandleError(const std::string& unique_id, uint32_t error_code);
    bool HandlePushPromise(std::unordered_map<std::string, std::string>& headers);
    void HandlePush(std::shared_ptr<IResponse> response, uint32_t error);

    // Shared body for the two DoRequest overloads. Templated on the handler
    // type so the const-callback and IAsyncClientHandler variants share a
    // single implementation (and a single fast-path/slow-path split).
    template <typename Handler>
    bool DoRequestImpl(const std::string& url, HttpMethod method, std::shared_ptr<IRequest> request, Handler handler);

private:
    std::shared_ptr<IQuicClient> quic_;

    // conn_map_ is read on the user (caller) thread for the DoRequest()
    // fast-path (an existing connection to `host` is reused without hopping
    // onto the QUIC event loop) and written exclusively on the event-loop
    // thread (OnConnection). The shared_mutex lets the hot read path scale
    // across many concurrent caller threads while still serialising the
    // rare insert/erase against rehash.
    mutable std::shared_mutex conn_map_mu_;
    std::unordered_map<std::string, std::shared_ptr<ClientConnection>> conn_map_;

    // Connections whose destruction has been deferred. The QUIC close
    // callback is delivered synchronously from the frame-processing path and
    // is often entered from a ClientConnection method itself (server GOAWAY ->
    // HandleGoaway -> Shutdown -> Close), so dropping the last reference
    // during the erase would run ~ClientConnection while that method is still
    // on the stack and let the remaining frames of the same datagram dispatch
    // into freed HTTP/3 streams. Park the pointer here and release it on the
    // next connection event instead.
    std::vector<std::shared_ptr<ClientConnection>> closing_conns_;

    http_response_handler push_handler_;
    http_push_promise_handler push_promise_handler_;
    error_handler error_handler_;
    Http3Settings settings_;
    Http3ClientConfig config_;  // Store config for connection timeout

    // Track connections that are in closing state
    // These three are touched by both the caller thread (Client::Close) and the
    // QUIC worker/event-loop thread (connection-close callbacks and the
    // fallback timer), so they are atomic to avoid TSan data races.
    std::atomic<bool> is_closing_{false};
    // Number of quic-connections still pending kConnectionClose callback while
    // the client is in graceful-shutdown. When this reaches 0 we can Destroy()
    // the underlying quic client immediately instead of waiting for the
    // kConnectionCloseDestroyTimeoutMs fallback timer.
    std::atomic<uint32_t> pending_close_count_{0};
    std::atomic<bool> destroy_scheduled_{false};

    // Migration callback to forward to all connections
    migration_callback migration_cb_;

    struct WaitRequestContext {
        std::string host;
        std::shared_ptr<IRequest> request;
        std::variant<http_response_handler, std::shared_ptr<IAsyncClientHandler>> handler;

        // Helper to check if async handler
        bool IsAsync() const { return std::holds_alternative<std::shared_ptr<IAsyncClientHandler>>(handler); }

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
    // host => queue of waiting requests
    std::unordered_map<std::string, std::queue<WaitRequestContext>> wait_request_map_;
    // Guards wait_request_map_. Mutated by DoRequest() on the caller (user)
    // thread AND by the QUIC event-loop/worker thread inside OnConnection()/
    // OnConnectionComplete() (find/erase/pop) AND cleared by the destructor.
    // Without a lock the unordered_map's internal state races (hash-table
    // rehash mid-read, etc.) — a ThreadSanitizer data race.
    std::mutex wait_request_map_mu_;
};

}  // namespace http3
}  // namespace quicx

#endif
