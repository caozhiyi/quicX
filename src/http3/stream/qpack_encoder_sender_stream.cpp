#include "common/buffer/buffer.h"
#include "http3/stream/qpack_encoder_sender_stream.h"

namespace quicx {
namespace http3 {

QpackEncoderSenderStream::QpackEncoderSenderStream(const std::shared_ptr<quic::IQuicSendStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler):
    IStream(error_handler), stream_(stream) {}

QpackEncoderSenderStream::~QpackEncoderSenderStream() {
    if (stream_) stream_->Close();
}

bool QpackEncoderSenderStream::EnsureStreamPreamble() {
    if (wrote_type_) return true;
    // QPACK encoder stream type 0x02 (varint)
    uint8_t pre[1] = {0x02};
    auto buf = std::make_shared<common::Buffer>(pre, sizeof(pre));
    buf->MoveWritePt(sizeof(pre));
    if (stream_->Send(buf) <= 0) return false;
    wrote_type_ = true;
    return true;
}

bool QpackEncoderSenderStream::SendInstructions(const std::vector<uint8_t>& blob) {
    if (!EnsureStreamPreamble()) return false;
    if (blob.empty()) return true;
    auto buf = std::make_shared<common::Buffer>((uint8_t*)blob.data(), blob.size());
    buf->MoveWritePt(blob.size());
    return stream_->Send(buf) > 0;
}

}
}


