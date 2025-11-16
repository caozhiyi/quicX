#include "http3/stream/type.h"
#include "http3/frame/qpack_decoder_frames.h"
#include "http3/stream/qpack_decoder_sender_stream.h"

namespace quicx {
namespace http3 {

// Lightweight helper objects to emit unidirectional stream type bytes
// for QPACK decoder streams.
class QpackDecoderStreamPreamble {
public:
    static bool Encode(const std::shared_ptr<common::IBuffer>& buffer) {
        uint8_t t = static_cast<uint8_t>(StreamType::kQpackDecoder);
        return buffer && buffer->Write(&t, 1) == 1;
        }
};
    
QpackDecoderSenderStream::QpackDecoderSenderStream(const std::shared_ptr<IQuicSendStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler):
    ISendStream(StreamType::kQpackDecoder, stream, error_handler) {
    
}

QpackDecoderSenderStream::~QpackDecoderSenderStream() {
    if (stream_) {
        stream_->Close();
    }
}

bool QpackDecoderSenderStream::SendSectionAck(uint64_t header_block_id) {
    if (!EnsureStreamPreamble()) {
        return false;
    }
    // header_block_id is composed as (stream_id << 32) | section_number
    uint64_t stream_id = static_cast<uint64_t>(static_cast<uint32_t>(header_block_id >> 32));
    uint64_t section_number = static_cast<uint64_t>(static_cast<uint32_t>(header_block_id & 0xffffffffULL));

    quicx::http3::QpackSectionAckFrame frame;
    frame.Set(stream_id, section_number);
    auto buf = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    if (!frame.Encode(buf)) {
        return false;
    }
    return stream_->Send(buf) > 0;
}

bool QpackDecoderSenderStream::SendStreamCancel(uint64_t header_block_id) {
    if (!EnsureStreamPreamble()) {
        return false;
    }
    uint64_t stream_id = static_cast<uint64_t>(static_cast<uint32_t>(header_block_id >> 32));
    uint64_t section_number = static_cast<uint64_t>(static_cast<uint32_t>(header_block_id & 0xffffffffULL));
    quicx::http3::QpackStreamCancellationFrame frame;
    frame.Set(stream_id, section_number);
    auto buf = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    if (!frame.Encode(buf)) {
        return false;
    }
    return stream_->Send(buf) > 0;
}

bool QpackDecoderSenderStream::SendInsertCountIncrement(uint64_t delta) {
    if (!EnsureStreamPreamble()) {
        return false;
    }
    quicx::http3::QpackInsertCountIncrementFrame frame;
    frame.Set(delta);
    auto buf = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    if (!frame.Encode(buf)) {
        return false;
    }
    return stream_->Send(buf) > 0;
}

}
}


