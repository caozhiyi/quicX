#ifndef HTTP3_CONNECTION_SERVER_CONNECTION
#define HTTP3_CONNECTION_SERVER_CONNECTION

#include <memory>
#include <functional>
#include <unordered_map>

#include "http3/include/type.h"
#include "http3/include/if_server.h"
#include "http3/qpack/qpack_encoder.h"
#include "quic/include/if_quic_stream.h"
#include "http3/connection/if_connection.h"
#include "quic/include/if_quic_connection.h"
#include "http3/stream/control_sender_stream.h"
#include "http3/stream/control_receiver_stream.h"

namespace quicx {
namespace http3 {

class ServerConnection:
    public IConnection {
public:
    ServerConnection(const std::string& unique_id,
        const std::shared_ptr<quic::IQuicConnection>& quic_connection,
        const std::function<void(const std::string& unique_id, uint32_t error_code)>& error_handler,
        const http_handler& http_handler);
    virtual ~ServerConnection();

    // send push promise
    virtual bool SendPushPromise(const std::unordered_map<std::string, std::string>& headers);

    // send push
    virtual bool SendPush(const IResponse& response);

private:
    void HandleStream(std::shared_ptr<quic::IQuicStream> stream, uint32_t error_code);
    // handle goaway frame
    void HandleGoaway(uint64_t id);
    // handle max push id frame
    void HandleMaxPushId(uint64_t max_push_id);
    // handle cancel push frame
    void HandleCancelPush(uint64_t push_id);
    // handle error
    void HandleError(uint64_t stream_id, uint32_t error_code);

private:
    uint64_t max_push_id_;
    http_handler http_handler_;
};

}
}

#endif
