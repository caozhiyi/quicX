#ifndef HTTP3_STREAM_QPACK_ENCODER_SENDER_STREAM
#define HTTP3_STREAM_QPACK_ENCODER_SENDER_STREAM

#include <vector>
#include <memory>
#include "http3/stream/if_stream.h"
#include "quic/include/if_quic_send_stream.h"

namespace quicx {
namespace http3 {

class QpackEncoderSenderStream: public IStream {
public:
    explicit QpackEncoderSenderStream(const std::shared_ptr<quic::IQuicSendStream>& stream,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler);
    ~QpackEncoderSenderStream();

    virtual StreamType GetType() override { return StreamType::kControl; }
    virtual uint64_t GetStreamID() override { return stream_->GetStreamID(); }

    // Write QPACK encoder stream type (0x02) and send instruction bytes
    bool SendInstructions(const std::vector<uint8_t>& blob);

private:
    bool EnsureStreamPreamble();

private:
    bool wrote_type_ {false};
    std::shared_ptr<quic::IQuicSendStream> stream_;
};

}
}

#endif


