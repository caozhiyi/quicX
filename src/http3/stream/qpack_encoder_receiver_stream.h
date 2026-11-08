#ifndef HTTP3_STREAM_QPACK_ENCODER_RECEIVER_STREAM
#define HTTP3_STREAM_QPACK_ENCODER_RECEIVER_STREAM

#include <functional>
#include <memory>
#include <quicx/quic/if_quic_recv_stream.h>

#include "http3/qpack/blocked_registry.h"
#include "http3/qpack/qpack_decoder.h"
#include "http3/stream/if_recv_stream.h"

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
class QpackEncoderReceiverStream: public IRecvStream {
public:
    QpackEncoderReceiverStream(const std::shared_ptr<IQuicRecvStream>& stream,
        const std::shared_ptr<QpackDecoder>& qpack_decoder,
        const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler);
    ~QpackEncoderReceiverStream();

    virtual void OnData(std::shared_ptr<IBufferRead> data, bool is_last, uint32_t error) override;

private:
    std::shared_ptr<QpackDecoder> qpack_decoder_;  // P1-H4d: peer encoder stream drives OUR decoder table
    std::shared_ptr<QpackBlockedRegistry> blocked_registry_;
    // Parse encoder stream instructions
    void ParseEncoderInstructions(std::shared_ptr<IBufferRead> data);

    // P1-H5a: instruction-in-waiting cap. The QUIC stream is a byte stream;
    // an instruction may be split across STREAM frames. While waiting for
    // the tail the unparsed bytes stay pinned in the receive buffer, and a
    // peer that declares an absurd string length (e.g. a 5-byte varint
    // claiming gigabytes) would pin memory forever. Past this cap the tail
    // is treated as malformed and the stream errors out instead.
    static constexpr uint32_t kMaxPendingInstrBytes = 64 * 1024;
};

}  // namespace http3
}  // namespace quicx

#endif  // HTTP3_STREAM_QPACK_ENCODER_RECEIVER_STREAM
