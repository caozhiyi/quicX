#ifndef HTTP3_CONNECTION_CLIENT_CONNECTION
#define HTTP3_CONNECTION_CLIENT_CONNECTION

#include <functional>
#include <memory>
#include <unordered_map>

#include "http3/connection/if_connection.h"
#include "http3/include/if_async_handler.h"
#include "http3/include/type.h"
#include "http3/stream/control_client_sender_stream.h"
#include "http3/stream/control_receiver_stream.h"
#include "quic/include/if_quic_connection.h"

namespace quicx {
namespace http3 {

class ClientConnection: public IConnection, public std::enable_shared_from_this<ClientConnection> {
public:
    ClientConnection(const std::string& unique_id, const Http3Settings& settings,
        const std::shared_ptr<IQuicConnection>& quic_connection,
        const std::function<void(const std::string& unique_id, uint32_t error_code)>& error_handler,
        const std::function<bool(std::unordered_map<std::string, std::string>& headers)>& push_promise_handler,
        const http_response_handler& push_handler);
    virtual ~ClientConnection();

    /**
     * @brief Send HTTP request in complete mode (entire response body buffered)
     * @param request Request object
     * @param handler Response handler callback (called when response is complete)
     * @return true if request was sent successfully
     */
    virtual bool DoRequest(std::shared_ptr<IRequest> request, const http_response_handler& handler);

    /**
     * @brief Send HTTP request with async handler for streaming response
     * @param request Request object
     * @param handler Async handler for streaming response processing
     * @return true if request was sent successfully
     */
    virtual bool DoRequest(std::shared_ptr<IRequest> request, std::shared_ptr<IAsyncClientHandler> handler);

    virtual void SetMaxPushID(uint64_t max_push_id);
    virtual void CancelPush(uint64_t push_id);

private:
    // Helper to create and send RequestStream with http_response_handler
    void CreateAndSendRequestStream(std::shared_ptr<IRequest> request, std::shared_ptr<IQuicStream> stream,
        const http_response_handler& handler);
    // Helper to create and send RequestStream with IAsyncClientHandler
    void CreateAndSendRequestStream(std::shared_ptr<IRequest> request, std::shared_ptr<IQuicStream> stream,
        std::shared_ptr<IAsyncClientHandler> handler);

    void HandleStream(std::shared_ptr<IQuicStream> stream, uint32_t error);
    // Callback when stream type is identified (RFC 9114 Section 6.2)
    void OnStreamTypeIdentified(
        uint64_t stream_type, std::shared_ptr<IQuicRecvStream> stream, std::shared_ptr<IBufferRead> data);
    // handle goaway frame
    void HandleGoaway(uint64_t id);
    // handle error
    void HandleError(uint64_t stream_id, uint32_t error_code);
    // handle push promise
    void HandlePushPromise(std::unordered_map<std::string, std::string>& headers, uint64_t push_id);

private:
    http_response_handler push_handler_;
    std::function<bool(std::unordered_map<std::string, std::string>&)> push_promise_handler_;

    std::shared_ptr<ControlClientSenderStream> control_sender_stream_;
    std::shared_ptr<ControlReceiverStream> control_recv_stream_;
};

}  // namespace http3
}  // namespace quicx

#endif
