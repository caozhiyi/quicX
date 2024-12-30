#ifndef HTTP3_STREAM_CONTROL_SENDER_STREAM
#define HTTP3_STREAM_CONTROL_SENDER_STREAM

#include <memory>
#include <unordered_map>

#include "http3/stream/type.h"
#include "http3/stream/if_stream.h"
#include "quic/include/if_quic_send_stream.h"

namespace quicx {
namespace http3 {

class ControlSenderStream:
    public IStream {
public:
    ControlSenderStream(std::shared_ptr<quic::IQuicSendStream> stream);
    virtual ~ControlSenderStream();

    virtual StreamType GetType() { return StreamType::ST_CONTROL; }

    // Send SETTINGS frame
    virtual bool SendSettings(const std::unordered_map<SettingsType, uint64_t>& settings);

    // Send GOAWAY frame
    virtual bool SendGoaway(uint64_t id);

protected:
    std::shared_ptr<quic::IQuicSendStream> stream_;
};

}
}

#endif
