#ifndef HTTP3_STREAM_CONTROL_STREAM
#define HTTP3_STREAM_CONTROL_STREAM

#include <memory>
#include <unordered_map>

#include "http3/stream/type.h"
#include "http3/stream/if_stream.h"
#include "quic/include/if_quic_send_stream.h"

namespace quicx {
namespace http3 {

class ControlStream:
    public IStream {
public:
    ControlStream(std::shared_ptr<quic::IQuicSendStream> stream);
    virtual ~ControlStream();

    virtual StreamType GetType() { return StreamType::ST_CONTROL; }

    // Send SETTINGS frame
    virtual bool SendSettings(const std::unordered_map<SettingsType, uint64_t>& settings);

    // Send GOAWAY frame
    virtual bool SendGoaway(uint64_t id);

    // Send MAX_PUSH_ID frame
    virtual bool SendMaxPushId(uint64_t push_id);

    // Send CANCEL_PUSH frame
    virtual bool SendCancelPush(uint64_t push_id);

    virtual void OnFrame(std::shared_ptr<IFrame> frame);

private:
    std::shared_ptr<quic::IQuicSendStream> stream_;
};

}
}

#endif
