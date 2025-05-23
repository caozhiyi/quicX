#ifndef HTTP3_CONNECTION_SERVER_CONNECTION
#define HTTP3_CONNECTION_SERVER_CONNECTION

#include <memory>
#include <functional>
#include <unordered_map>

#include "http3/include/type.h"
#include "http3/include/if_server.h"
#include "http3/qpack/qpack_encoder.h"
#include "quic/include/if_quic_stream.h"
#include "quic/include/if_quic_server.h"
#include "http3/stream/response_stream.h"
#include "http3/connection/if_connection.h"
#include "quic/include/if_quic_connection.h"
#include "http3/stream/control_sender_stream.h"
#include "http3/stream/control_server_receiver_stream.h"

namespace quicx {
namespace http3 {

class ServerConnection:
    public IConnection {
public:
    ServerConnection(const std::string& unique_id,
        const Http3Settings& settings,
        std::shared_ptr<quic::IQuicServer> quic_server,
        const std::shared_ptr<quic::IQuicConnection>& quic_connection,
        const std::function<void(const std::string& unique_id, uint32_t error_code)>& error_handler,
        const http_handler& http_handler);
    virtual ~ServerConnection();

private:
    // send push
    bool SendPush(std::shared_ptr<IResponse> response);
    // handle http request
    void HandleHttp(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response, std::shared_ptr<ResponseStream> response_stream);
    // handle stream status
    void HandleStream(std::shared_ptr<quic::IQuicStream> stream, uint32_t error_code);
    // handle goaway frame
    void HandleGoaway(uint64_t id);
    // handle max push id frame
    void HandleMaxPushId(uint64_t max_push_id);
    // handle cancel push frame
    void HandleCancelPush(uint64_t push_id);
    // handle error
    void HandleError(uint64_t stream_id, uint32_t error_code);
    // handle timer
    void HandleTimer();

private:
    bool IsEnabledPush() const;
    bool CanPush() const;

private:
    uint64_t max_push_id_;
    uint64_t next_push_id_;
    uint64_t send_limit_push_id_;
    http_handler http_handler_;
    std::shared_ptr<quic::IQuicServer> quic_server_;
    // push responses, push id -> response
    std::unordered_map<uint64_t, std::shared_ptr<IResponse>> push_responses_;

    std::shared_ptr<ControlSenderStream> control_sender_stream_;
    std::shared_ptr<ControlServerReceiverStream> control_recv_stream_;
};

}
}

#endif
