#include "common/log/log.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/qpack/blocked_registry.h"
#include "http3/stream/qpack_encoder_receiver_stream.h"

namespace quicx {
namespace http3 {

QpackEncoderReceiverStream::QpackEncoderReceiverStream(
    const std::shared_ptr<IQuicRecvStream>& stream,
    const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler):
    IRecvStream(StreamType::kQpackEncoder, stream, error_handler),
    qpack_encoder_(qpack_encoder),
    blocked_registry_(blocked_registry) {
    stream_->SetStreamReadCallBack(
        std::bind(&QpackEncoderReceiverStream::OnData, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    
    common::LOG_DEBUG("QpackEncoderReceiverStream created for stream %llu", stream_->GetStreamID());
}

QpackEncoderReceiverStream::~QpackEncoderReceiverStream() {
    // Note: Do NOT call stream_->Reset() here during destruction.
    // See QpackDecoderSenderStream destructor comment for details.
    stream_.reset();
}

void QpackEncoderReceiverStream::OnData(std::shared_ptr<IBufferRead> data, bool is_last, uint32_t error) {
    if (error != 0) {
        common::LOG_ERROR("QpackEncoderReceiverStream::OnData error: %d on stream %llu", 
                         error, stream_->GetStreamID());
        error_handler_(stream_->GetStreamID(), error);
        return;
    }

    if (data->GetDataLength() == 0) {
        return;
    }

    common::LOG_DEBUG("QpackEncoderReceiverStream: received %u bytes on stream %llu", 
                     data->GetDataLength(), stream_->GetStreamID());

    // Parse QPACK encoder instructions
    ParseEncoderInstructions(data);
}

void QpackEncoderReceiverStream::ParseEncoderInstructions(std::shared_ptr<IBufferRead> data) {
    // RFC 9204 Section 4.3: Encoder instructions include:
    // - Set Dynamic Table Capacity
    // - Insert With Name Reference
    // - Insert Without Name Reference
    // - Duplicate
    
    // Use the connection-level shared QpackEncoder to decode encoder instructions
    // This ensures dynamic table updates persist across invocations (RFC 9204 §4.2)
    auto buffer = std::dynamic_pointer_cast<common::IBuffer>(data);
    if (!qpack_encoder_->DecodeEncoderInstructions(buffer)) {
        common::LOG_ERROR("QpackEncoderReceiverStream: failed to decode encoder instructions on stream %llu", 
                         stream_->GetStreamID());
        return;
    }
    
    common::LOG_DEBUG("QpackEncoderReceiverStream: successfully parsed encoder instructions on stream %llu", 
                     stream_->GetStreamID());
    
    // Notify any blocked streams that new table entries are available
    if (blocked_registry_) {
        blocked_registry_->NotifyAll();
    }
}

}
}

