#include "common/log/log.h"
#include "http3/stream/qpack_decoder_receiver_stream.h"
#include "http3/qpack/blocked_registry.h"
#include "http3/qpack/util.h"

namespace quicx {
namespace http3 {

QpackDecoderReceiverStream::QpackDecoderReceiverStream(const std::shared_ptr<quic::IQuicRecvStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler)
    : IStream(error_handler), stream_(stream) {
    stream_->SetStreamReadCallBack(std::bind(&QpackDecoderReceiverStream::OnData, this, std::placeholders::_1, std::placeholders::_2));
}

QpackDecoderReceiverStream::~QpackDecoderReceiverStream() {
    if (stream_) stream_->Reset(0);
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
    // very simplified: frames as [type:1][varint payload]
    while (data->GetDataLength() > 0) {
        uint8_t t = 0; if (data->Read(&t, 1) != 1) return;
        if (t == 0x00) {
            // Section Acknowledgement: [stream_id varint][section_number varint]
            uint8_t fb = 0; uint64_t sid = 0; uint64_t secno = 0;
            if (!QpackDecodePrefixedInteger(data, 8, fb, sid)) return;
            if (!QpackDecodePrefixedInteger(data, 8, fb, secno)) return;
            uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(sid)) << 32) | static_cast<uint64_t>(static_cast<uint32_t>(secno));
            ::quicx::http3::QpackBlockedRegistry::Instance().Ack(key);
        } else if (t == 0x01) {
            // Stream Cancellation: [stream_id varint][section_number varint]
            uint8_t fb = 0; uint64_t sid = 0; uint64_t secno = 0;
            if (!QpackDecodePrefixedInteger(data, 8, fb, sid)) return;
            if (!QpackDecodePrefixedInteger(data, 8, fb, secno)) return;
            uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(sid)) << 32) | static_cast<uint64_t>(static_cast<uint32_t>(secno));
            ::quicx::http3::QpackBlockedRegistry::Instance().Remove(key);
        } else if (t == 0x02) {
            // Insert Count Increment: [delta varint]
            uint8_t fb = 0; uint64_t delta = 0;
            if (!QpackDecodePrefixedInteger(data, 8, fb, delta)) return;
            insert_count_ += delta;
            extern void QpackNotifyBlockedResume();
            QpackNotifyBlockedResume();
        } else {
            // unknown -> stop
            break;
        }
    }
}

}
}


