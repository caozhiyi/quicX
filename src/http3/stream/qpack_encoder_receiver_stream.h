#ifndef HTTP3_STREAM_QPACK_ENCODER_RECEIVER_STREAM
#define HTTP3_STREAM_QPACK_ENCODER_RECEIVER_STREAM

#include <memory>
#include <functional>
#include "http3/stream/if_stream.h"
#include "http3/qpack/blocked_registry.h"
#include "quic/include/if_quic_recv_stream.h"

namespace quicx {
namespace http3 {

/**
 * @brief QpackEncoderReceiverStream receives QPACK encoder instructions from peer
 * 
 * According to RFC 9204 Section 4.2:
 * The encoder stream (type 0x02) carries instructions from encoder to decoder:
 * - Insert With Name Reference
 * - Insert Without Name Reference
 * - Duplicate
 * - Set Dynamic Table Capacity
 * 
 * This stream receives these instructions and updates the local dynamic table.
 */
class QpackEncoderReceiverStream : public IStream {
public:
    QpackEncoderReceiverStream(
        const std::shared_ptr<quic::IQuicRecvStream>& stream,
        const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler);
    ~QpackEncoderReceiverStream();

    virtual StreamType GetType() override { return StreamType::kQpackEncoder; }
    virtual uint64_t GetStreamID() override { return stream_->GetStreamID(); }

protected:
    virtual void OnData(std::shared_ptr<common::IBufferRead> data, uint32_t error);

private:
    std::shared_ptr<QpackBlockedRegistry> blocked_registry_;
    std::shared_ptr<quic::IQuicRecvStream> stream_;
    
    // Parse encoder stream instructions
    void ParseEncoderInstructions(std::shared_ptr<common::IBufferRead> data);
};

}
}

#endif

