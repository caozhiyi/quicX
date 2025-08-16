#include "http3/qpack/util.h"
#include "common/buffer/buffer.h"
#include "http3/frame/qpack_decoder_frames.h"
#include "http3/stream/qpack_decoder_sender_stream.h"

namespace quicx {
namespace http3 {

// Lightweight helper objects to emit unidirectional stream type bytes
// for QPACK decoder (0x03) streams.
class QpackDecoderStreamPreamble {
public:
    static bool Encode(const std::shared_ptr<common::IBufferWrite>& buffer) {
        uint8_t t = 0x03; // QPACK Decoder Stream type (varint value <= 63)
        return buffer && buffer->Write(&t, 1) == 1;
        }
};
    
QpackDecoderSenderStream::QpackDecoderSenderStream(const std::shared_ptr<quic::IQuicSendStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler)
    : IStream(error_handler), stream_(stream) {
    
}

QpackDecoderSenderStream::~QpackDecoderSenderStream() {
    if (stream_) {
        stream_->Close();
    }
}

bool QpackDecoderSenderStream::EnsureStreamPreamble() {
    if (wrote_type_) {
        return true;
    }
    uint8_t tmp[8] = {0};
    auto buf = std::make_shared<common::Buffer>(tmp, sizeof(tmp));
    if (!QpackDecoderStreamPreamble::Encode(buf)) {
        return false;
    }
    if (stream_->Send(buf) <= 0) {
        return false;
    }
    wrote_type_ = true;
    return true;
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
    uint8_t buf_mem[32] = {0};
    auto buf = std::make_shared<common::Buffer>(buf_mem, sizeof(buf_mem));
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
    uint8_t buf_mem[32] = {0};
    auto buf = std::make_shared<common::Buffer>(buf_mem, sizeof(buf_mem));
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
    uint8_t buf_mem[16] = {0};
    auto buf = std::make_shared<common::Buffer>(buf_mem, sizeof(buf_mem));
    if (!frame.Encode(buf)) {
        return false;
    }
    return stream_->Send(buf) > 0;
}

}
}


