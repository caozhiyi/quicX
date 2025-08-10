#ifndef HTTP3_STREAM_QPACK_DECODER_RECEIVER_STREAM
#define HTTP3_STREAM_QPACK_DECODER_RECEIVER_STREAM

#include <memory>
#include <functional>
#include "http3/stream/if_stream.h"
#include "quic/include/if_quic_recv_stream.h"

namespace quicx {
namespace http3 {

class QpackDecoderReceiverStream: public IStream {
public:
    QpackDecoderReceiverStream(const std::shared_ptr<quic::IQuicRecvStream>& stream,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler);
    ~QpackDecoderReceiverStream();

    virtual StreamType GetType() override { return StreamType::kControl; }
    virtual uint64_t GetStreamID() override { return stream_->GetStreamID(); }

protected:
    virtual void OnData(std::shared_ptr<common::IBufferRead> data, uint32_t error);

private:
    std::shared_ptr<quic::IQuicRecvStream> stream_;
    uint64_t insert_count_{0};
    // Parse decoder stream frames: Section Acknowledgement, Stream Cancellation, Insert Count Increment
    void ParseDecoderFrames(std::shared_ptr<common::IBufferRead> data);
};

}
}

#endif


