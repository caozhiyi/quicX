#ifndef HTTP3_CONNECTION_IF_CONNECTION
#define HTTP3_CONNECTION_IF_CONNECTION

#include <memory>
#include <functional>
#include <unordered_map>

#include "http3/include/type.h"
#include "http3/stream/if_stream.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/qpack/blocked_registry.h"
#include "quic/include/if_quic_connection.h"

namespace quicx {
namespace http3 {

class IConnection {
public:
    IConnection(const std::string& unique_id,
        const std::shared_ptr<quic::IQuicConnection>& quic_connection,
        const std::function<void(const std::string& unique_id, uint32_t error_code)>& error_handler);
    virtual ~IConnection();

    const std::string& GetUniqueId() const { return unique_id_; }

    // close connection
    virtual void Close(uint32_t error_code);

protected:
    // handle stream
    virtual void HandleStream(std::shared_ptr<quic::IQuicStream> stream, uint32_t error_code) = 0;
    // handle error
    virtual void HandleError(uint64_t stream_id, uint32_t error_code) = 0;
    // handle settings
    virtual void HandleSettings(const std::unordered_map<uint16_t, uint64_t>& settings);
    
    static const std::unordered_map<uint16_t, uint64_t> AdaptSettings(const Http3Settings& settings);

protected:
    // indicate the unique id of the connection
    std::string unique_id_;
    std::shared_ptr<QpackBlockedRegistry> blocked_registry_;
    std::function<void(const std::string& unique_id, uint32_t error_code)> error_handler_;
    std::unordered_map<uint16_t, uint64_t> settings_;
    std::unordered_map<uint64_t, std::shared_ptr<IStream>> streams_;

    std::shared_ptr<QpackEncoder> qpack_encoder_;

    std::shared_ptr<quic::IQuicConnection> quic_connection_;
};

}
}

#endif
