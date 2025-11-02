#ifndef HTTP3_STREAM_CONTROL_RECEIVER_STREAM
#define HTTP3_STREAM_CONTROL_RECEIVER_STREAM

#include <memory>
#include <functional>
#include <unordered_map>

#include "http3/stream/type.h"
#include "http3/frame/if_frame.h"
#include "http3/stream/if_recv_stream.h"
#include "quic/include/if_quic_recv_stream.h"

namespace quicx {
namespace http3 {

class ControlReceiverStream:
    public IRecvStream {
public:
    ControlReceiverStream(const std::shared_ptr<quic::IQuicRecvStream>& stream,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
        const std::function<void(uint64_t id)>& goaway_handler,
        const std::function<void(const std::unordered_map<uint16_t, uint64_t>& settings)>& settings_handler);
    virtual ~ControlReceiverStream();

    virtual void OnData(std::shared_ptr<common::IBufferRead> data, uint32_t error) override;

protected:
    virtual void HandleFrame(std::shared_ptr<IFrame> frame);
    // Handle raw QPACK instruction data on control stream (demo)
    void HandleRawData(std::shared_ptr<common::IBufferRead> data);

protected:
    std::function<void(uint64_t id)> goaway_handler_;
    std::function<void(const std::unordered_map<uint16_t, uint64_t>& settings)> settings_handler_;
    std::function<void(std::shared_ptr<common::IBufferRead>)> qpack_instr_handler_;
    
};

}
}

#endif
