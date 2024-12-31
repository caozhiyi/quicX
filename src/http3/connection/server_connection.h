#ifndef HTTP3_CONNECTION_SERVER_CONNECTION
#define HTTP3_CONNECTION_SERVER_CONNECTION

#include <memory>
#include <functional>
#include <unordered_map>

#include "http3/include/type.h"
#include "http3/include/if_server.h"
#include "http3/qpack/qpack_encoder.h"
#include "quic/include/if_quic_stream.h"
#include "quic/include/if_quic_connection.h"
#include "http3/stream/control_sender_stream.h"
#include "http3/stream/control_receiver_stream.h"

namespace quicx {
namespace http3 {

class ServerConnection {
public:
    ServerConnection(const std::shared_ptr<quic::IQuicConnection>& quic_connection,
        const std::function<void(int32_t error)>& error_handler,
        const http_handler& http_handler);
    virtual ~ServerConnection();

    // close connection
    virtual void Close(uint64_t error_code);

    // send push promise
    virtual bool SendPushPromise(const std::unordered_map<std::string, std::string>& headers);

    // send push
    virtual bool SendPush(const IResponse& response);

private:
    void HandleStream(std::shared_ptr<quic::IQuicStream> stream, uint32_t error);

    // handle goaway frame
    void HandleGoaway(uint64_t id);
    // handle settings frame
    void HandleSettings(const std::unordered_map<uint16_t, uint64_t>& settings);
    // handle max push id frame
    void HandleMaxPushId(uint64_t max_push_id);
    // handle cancel push frame
    void HandleCancelPush(uint64_t push_id);
    // handle error
    void HandleError(uint64_t stream_id, int32_t error);

private:
    std::function<void(int32_t)> error_handler_;
    http_handler http_handler_;
    
    std::unordered_map<uint16_t, uint64_t> settings_;
    std::unordered_map<uint64_t, std::shared_ptr<IStream>> streams_;

    std::shared_ptr<QpackEncoder> qpack_encoder_;

    std::shared_ptr<quic::IQuicConnection> quic_connection_;
    std::shared_ptr<ControlReceiverStream> control_recv_stream_;
    std::shared_ptr<ControlSenderStream> control_sender_stream_;

    uint64_t max_push_id_;
};

}
}

#endif
