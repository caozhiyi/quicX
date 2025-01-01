#ifndef HTTP3_CONNECTION_CLIENT_CONNECTION
#define HTTP3_CONNECTION_CLIENT_CONNECTION

#include <memory>
#include <functional>
#include <unordered_map>

#include "common/timer/timer_task.h"
#include "http3/include/if_client.h"
#include "http3/qpack/qpack_encoder.h"
#include "quic/include/if_quic_stream.h"
#include "quic/include/if_quic_connection.h"
#include "http3/stream/control_receiver_stream.h"
#include "http3/stream/control_client_sender_stream.h"

namespace quicx {
namespace http3 {

class ClientConnection {
public:
    ClientConnection(const std::shared_ptr<quic::IQuicConnection>& quic_connection,
        const std::function<void(uint32_t error)>& error_handler,
        const std::function<void(std::unordered_map<std::string, std::string>&)>& push_promise_handler,
        const http_response_handler& push_handler);
    virtual ~ClientConnection();

    // close connection
    virtual void Close(uint64_t error_code);

    // send request
    virtual bool DoRequest(const std::string& url, const IRequest& request, const http_response_handler& handler);
    virtual void SetMaxPushID(uint64_t max_push_id);
    virtual void CancelPush(uint64_t push_id);

private:
    void HandleStream(std::shared_ptr<quic::IQuicStream> stream, uint32_t error);

    // handle settings frame
    void HandleSettings(const std::unordered_map<uint16_t, uint64_t>& settings);
    // handle goaway frame
    void HandleGoaway(uint64_t id);
    // handle error
    void HandleError(uint64_t stream_id, uint32_t error);
    // handle push promise
    void HandlePushPromise(std::unordered_map<std::string, std::string>& headers);
    
private:
    std::function<void(int32_t)> error_handler_;
    std::function<void(std::unordered_map<std::string, std::string>&)> push_promise_handler_;
    http_response_handler push_handler_;
    
    std::unordered_map<uint16_t, uint64_t> settings_;
    std::unordered_map<uint64_t, std::shared_ptr<IStream>> streams_;

    std::shared_ptr<QpackEncoder> qpack_encoder_;

    std::shared_ptr<quic::IQuicConnection> quic_connection_;
    std::shared_ptr<ControlReceiverStream> control_recv_stream_;
    std::shared_ptr<ControlClientSenderStream> control_sender_stream_;
    
};

}
}

#endif
