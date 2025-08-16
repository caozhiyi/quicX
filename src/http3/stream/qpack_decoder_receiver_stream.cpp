#include "common/log/log.h"
#include "http3/qpack/blocked_registry.h"
#include "http3/frame/qpack_decoder_frames.h"
#include "http3/stream/qpack_decoder_receiver_stream.h"

namespace quicx {
namespace http3 {

QpackDecoderReceiverStream::QpackDecoderReceiverStream(const std::shared_ptr<quic::IQuicRecvStream>& stream,
    const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler):
    IStream(error_handler), stream_(stream), blocked_registry_(blocked_registry) {
    stream_->SetStreamReadCallBack(std::bind(&QpackDecoderReceiverStream::OnData, this, std::placeholders::_1, std::placeholders::_2));
}

QpackDecoderReceiverStream::~QpackDecoderReceiverStream() {
    if (stream_) {
        stream_->Reset(0);
    }
}

void QpackDecoderReceiverStream::OnData(std::shared_ptr<common::IBufferRead> data, uint32_t error) {
    if (error != 0) {
        common::LOG_ERROR("QpackDecoderReceiverStream::OnData error: %d", error);
        error_handler_(stream_->GetStreamID(), error);
        return;
    }
    // QPACK encoder stream starts with varint type=0x02; consume first byte if equals
    if (data->GetDataLength() > 0) {
        uint8_t maybe_type = 0; data->Read(&maybe_type, 1);
        if (maybe_type != 0x02) {
            // not a qpack encoder stream start; ignore
        }
    }
    // Decoder stream type is 0x03; read and parse decoder frames
    ParseDecoderFrames(data);
}

void QpackDecoderReceiverStream::ParseDecoderFrames(std::shared_ptr<common::IBufferRead> data) {
    std::vector<std::shared_ptr<IQpackDecoderFrame>> frames;
    if (!DecodeQpackDecoderFrames(data, frames)) {
        common::LOG_ERROR("QpackDecoderReceiverStream::ParseDecoderFrames error: %d", data->GetDataLength());
        return;
    }
    for (const auto& frame : frames) {
        if (frame->GetType() == static_cast<uint8_t>(QpackDecoderInstrType::kSectionAck)) {
            QpackSectionAckFrame* f = dynamic_cast<QpackSectionAckFrame*>(frame.get());
            uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(f->GetStreamId())) << 32) | static_cast<uint64_t>(static_cast<uint32_t>(f->GetSectionNumber()));
            blocked_registry_->Ack(key);

        } else if (frame->GetType() == static_cast<uint8_t>(QpackDecoderInstrType::kStreamCancellation)) {
            QpackStreamCancellationFrame* f = dynamic_cast<QpackStreamCancellationFrame*>(frame.get());
            uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(f->GetStreamId())) << 32) | static_cast<uint64_t>(static_cast<uint32_t>(f->GetSectionNumber()));
            blocked_registry_->Remove(key);
            
        } else if (frame->GetType() == static_cast<uint8_t>(QpackDecoderInstrType::kInsertCountInc)) {
            QpackInsertCountIncrementFrame* f = dynamic_cast<QpackInsertCountIncrementFrame*>(frame.get());
            insert_count_ += f->GetDelta();
            blocked_registry_->NotifyAll();
        }
    }
}

}
}


