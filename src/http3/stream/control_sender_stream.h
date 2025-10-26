#ifndef HTTP3_STREAM_CONTROL_SENDER_STREAM
#define HTTP3_STREAM_CONTROL_SENDER_STREAM

#include <memory>
#include <functional>
#include <unordered_map>

#include "http3/stream/if_stream.h"
#include "quic/include/if_quic_send_stream.h"

namespace quicx {
namespace http3 {

class ControlSenderStream:
    public IStream {
public:
    ControlSenderStream(const std::shared_ptr<quic::IQuicSendStream>& stream,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler);
    virtual ~ControlSenderStream();

    virtual StreamType GetType() override { return StreamType::kControl; }
    virtual uint64_t GetStreamID() override { return stream_->GetStreamID(); }

    // Send SETTINGS frame
    virtual bool SendSettings(const std::unordered_map<uint16_t, uint64_t>& settings);

    // Send GOAWAY frame
    virtual bool SendGoaway(uint64_t id);

    // Send QPACK encoder instructions blob on control stream (demo integration)
    virtual bool SendQpackInstructions(const std::vector<uint8_t>& blob);

private:
    // Ensure stream type (0x00) is sent before any frames
    bool EnsureStreamPreamble();

protected:
    std::shared_ptr<quic::IQuicSendStream> stream_;
    bool wrote_type_ = false;  // Track whether stream type has been sent
};

}
}

#endif
