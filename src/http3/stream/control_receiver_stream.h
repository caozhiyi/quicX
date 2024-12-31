#ifndef HTTP3_STREAM_CONTROL_RECEIVER_STREAM
#define HTTP3_STREAM_CONTROL_RECEIVER_STREAM

#include <memory>
#include <functional>
#include <unordered_map>

#include "http3/stream/type.h"
#include "http3/stream/if_stream.h"
#include "quic/include/if_quic_recv_stream.h"

namespace quicx {
namespace http3 {

class ControlReceiverStream:
    public IStream {
public:
    ControlReceiverStream(const std::shared_ptr<quic::IQuicRecvStream>& stream,
        const std::function<void(int32_t)>& error_handler,
        const std::function<void(uint64_t id)>& goaway_handler,
        const std::function<void(const std::unordered_map<uint16_t, uint64_t>& settings)>& settings_handler);
    virtual ~ControlReceiverStream();

    virtual StreamType GetType() { return StreamType::ST_CONTROL; }

protected:
    virtual void OnData(std::shared_ptr<common::IBufferRead> data, uint32_t error);
    virtual void HandleFrame(std::shared_ptr<IFrame> frame);

protected:
    std::shared_ptr<quic::IQuicRecvStream> stream_;

    std::function<void(uint64_t id)> goaway_handler_;
    std::function<void(const std::unordered_map<uint16_t, uint64_t>& settings)> settings_handler_;
    
};

}
}

#endif
