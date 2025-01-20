#ifndef HTTP3_CONNECTION_CLIENT_CONNECTION
#define HTTP3_CONNECTION_CLIENT_CONNECTION

#include <memory>
#include <functional>
#include <unordered_map>

#include "common/timer/timer_task.h"
#include "http3/include/if_client.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/connection/if_connection.h"
#include "quic/include/if_quic_connection.h"
#include "http3/stream/control_receiver_stream.h"
#include "http3/stream/control_client_sender_stream.h"

namespace quicx {
namespace http3 {

class ClientConnection
    :public IConnection {
public:
    ClientConnection(const std::string& unique_id,
        const Http3Settings& settings,
        const std::shared_ptr<quic::IQuicConnection>& quic_connection,
        const std::function<void(const std::string& unique_id, uint32_t error_code)>& error_handler,
        const std::function<bool(std::unordered_map<std::string, std::string>& headers)>& push_promise_handler,
        const http_response_handler& push_handler);
    virtual ~ClientConnection();

    // send request
    virtual bool DoRequest(std::shared_ptr<IRequest> request, const http_response_handler& handler);
    virtual void SetMaxPushID(uint64_t max_push_id);
    virtual void CancelPush(uint64_t push_id);

private:
    void HandleStream(std::shared_ptr<quic::IQuicStream> stream, uint32_t error);
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

}
}

#endif
