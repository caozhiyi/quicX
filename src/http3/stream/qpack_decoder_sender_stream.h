#ifndef HTTP3_STREAM_QPACK_DECODER_SENDER_STREAM
#define HTTP3_STREAM_QPACK_DECODER_SENDER_STREAM

#include <memory>
#include <functional>
#include "http3/stream/if_send_stream.h"
#include "quic/include/if_quic_send_stream.h"

namespace quicx {
namespace http3 {

class QpackDecoderSenderStream:
    public ISendStream {
public:
    explicit QpackDecoderSenderStream(const std::shared_ptr<quic::IQuicSendStream>& stream,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler);
    ~QpackDecoderSenderStream();

    // Send Section Acknowledgement: type=0x00, payload=varint header_block_id
    bool SendSectionAck(uint64_t header_block_id);
    // Send Stream Cancellation: type=0x01, payload=varint header_block_id
    bool SendStreamCancel(uint64_t header_block_id);
    // Send Insert Count Increment: type=0x02, payload=varint delta
    bool SendInsertCountIncrement(uint64_t delta);
};

}
}

#endif


