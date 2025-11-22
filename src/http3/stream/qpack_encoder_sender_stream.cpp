#include "common/log/log.h"
#include "http3/stream/type.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/frame/qpack_encoder_frames.h"
#include "http3/stream/qpack_encoder_sender_stream.h"

namespace quicx {
namespace http3 {

// Lightweight helper objects to emit unidirectional stream type bytes
// for QPACK encoder streams.
class QpackEncoderStreamPreamble {
public:
    static bool Encode(const std::shared_ptr<common::IBuffer>& buffer) {
        uint8_t t = static_cast<uint8_t>(StreamType::kQpackEncoder);
        return buffer && buffer->Write(&t, 1) == 1;
    }
};

QpackEncoderSenderStream::QpackEncoderSenderStream(const std::shared_ptr<IQuicSendStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler):
    ISendStream(StreamType::kQpackEncoder, stream, error_handler) {}

QpackEncoderSenderStream::~QpackEncoderSenderStream() {
    if (stream_) {
        stream_->Close();
    }
}

bool QpackEncoderSenderStream::SendInstructions(const std::vector<std::pair<std::string,std::string>>& inserts) {
    if (inserts.empty()) {
        return true;
    }
    if (!EnsureStreamPreamble()) {
        return false;
    }
    auto send_buf = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    QpackEncoder enc;
    enc.EncodeEncoderInstructions(inserts, send_buf);

    if (!stream_->Flush()) {
        common::LOG_ERROR("QpackEncoderSenderStream::SendInstructions: flush failed");
        return false;
    }
    return true;
}

bool QpackEncoderSenderStream::SendSetCapacity(uint64_t capacity) {
    if (!EnsureStreamPreamble()) {
        return false;
    }

    auto buf = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    QpackSetCapacityFrame f;
    f.SetCapacity(capacity);
    if (!f.Encode(buf)) {
        return false;
    }   
    return stream_->Flush();
}

bool QpackEncoderSenderStream::SendInsertWithNameRef(bool is_static, uint64_t name_index, const std::string& value) {
    if (!EnsureStreamPreamble()) {
        return false;
    }

    auto buf = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    QpackInsertWithNameRefFrame f;
    f.Set(is_static, name_index, value);
    if (!f.Encode(buf)) {
        return false;
    }
    return stream_->Flush();
}

bool QpackEncoderSenderStream::SendInsertWithoutNameRef(const std::string& name, const std::string& value) {
    if (!EnsureStreamPreamble()) {
        return false;
    }
    
    auto buf = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    QpackInsertWithoutNameRefFrame f; 
    f.Set(name, value);
    if (!f.Encode(buf)) {
        return false;
    }
    return stream_->Flush();
}

bool QpackEncoderSenderStream::SendDuplicate(uint64_t index) {
    if (!EnsureStreamPreamble()) {
        return false;
    }
    auto buf = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    QpackDuplicateFrame f; f.Set(index);
    if (!f.Encode(buf)) {
        return false;
    }
    return stream_->Flush();
}

}
}


