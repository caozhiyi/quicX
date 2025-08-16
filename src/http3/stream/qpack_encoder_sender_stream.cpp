#include "common/buffer/buffer.h"
#include "http3/frame/qpack_encoder_frames.h"
#include "http3/stream/qpack_encoder_sender_stream.h"

namespace quicx {
namespace http3 {

// Lightweight helper objects to emit unidirectional stream type bytes
// for QPACK encoder (0x02) streams.
class QpackEncoderStreamPreamble {
public:
    static bool Encode(const std::shared_ptr<common::IBufferWrite>& buffer) {
        uint8_t t = 0x02; // QPACK Encoder Stream type (varint value <= 63)
        return buffer && buffer->Write(&t, 1) == 1;
    }
};

QpackEncoderSenderStream::QpackEncoderSenderStream(const std::shared_ptr<quic::IQuicSendStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler):
    IStream(error_handler), stream_(stream) {}

QpackEncoderSenderStream::~QpackEncoderSenderStream() {
    if (stream_) stream_->Close();
}

bool QpackEncoderSenderStream::EnsureStreamPreamble() {
    if (wrote_type_) return true;
    // Emit preamble using lightweight encoder
    uint8_t tmp[8] = {0};
    auto buf = std::make_shared<common::Buffer>(tmp, sizeof(tmp));
    if (!QpackEncoderStreamPreamble::Encode(buf)) return false;
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

bool QpackEncoderSenderStream::SendSetCapacity(uint64_t capacity) {
    if (!EnsureStreamPreamble()) {
        return false;
    }
    uint8_t tmp[32] = {0};
    auto buf = std::make_shared<common::Buffer>(tmp, sizeof(tmp));
    QpackSetCapacityFrame f; f.SetCapacity(capacity);
    if (!f.Encode(buf)) {
        return false;
    }
    return stream_->Send(buf) > 0;
}

bool QpackEncoderSenderStream::SendInsertWithNameRef(bool is_static, uint64_t name_index, const std::string& value) {
    if (!EnsureStreamPreamble()) {
        return false;
    }
    uint8_t tmp[512] = {0};
    auto buf = std::make_shared<common::Buffer>(tmp, sizeof(tmp));
    QpackInsertWithNameRefFrame f; f.Set(is_static, name_index, value);
    if (!f.Encode(buf)) {
        return false;
    }
    return stream_->Send(buf) > 0;
}

bool QpackEncoderSenderStream::SendInsertWithoutNameRef(const std::string& name, const std::string& value) {
    if (!EnsureStreamPreamble()) {
        return false;
    }
    uint8_t tmp[1024] = {0};
    auto buf = std::make_shared<common::Buffer>(tmp, sizeof(tmp));
    QpackInsertWithoutNameRefFrame f; f.Set(name, value);
    if (!f.Encode(buf)) {
        return false;
    }
    return stream_->Send(buf) > 0;
}

bool QpackEncoderSenderStream::SendDuplicate(uint64_t index) {
    if (!EnsureStreamPreamble()) {
        return false;
    }
    uint8_t tmp[32] = {0};
    auto buf = std::make_shared<common::Buffer>(tmp, sizeof(tmp));
    QpackDuplicateFrame f; f.Set(index);
    if (!f.Encode(buf)) {
        return false;
    }
    return stream_->Send(buf) > 0;
}

}
}


