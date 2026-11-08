#include "common/log/log.h"

#include "http3/frame/qpack_decoder_frames.h"
#include "http3/http/error.h"
#include "http3/qpack/blocked_registry.h"
#include "http3/stream/qpack_decoder_receiver_stream.h"

namespace quicx {
namespace http3 {

QpackDecoderReceiverStream::QpackDecoderReceiverStream(const std::shared_ptr<IQuicRecvStream>& stream,
    const std::shared_ptr<QpackEncoder>& local_encoder, const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler):
    IRecvStream(StreamType::kQpackDecoder, stream, error_handler),
    local_encoder_(local_encoder),
    blocked_registry_(blocked_registry) {
    stream_->SetStreamReadCallBack([this](auto a, auto b, auto c) { OnData(a, b, c); });
}

QpackDecoderReceiverStream::~QpackDecoderReceiverStream() {
    // Note: Do NOT call stream_->Reset() here during destruction.
    // See QpackDecoderSenderStream destructor comment for details.
    // P1-H7a: unregister the read callback BEFORE dropping the stream
    // reference — the underlying QUIC stream outlives this wrapper (the
    // connection holds it), and a callback still capturing `this` would
    // fire into a destroyed object.
    if (stream_) {
        stream_->SetStreamReadCallBack(nullptr);
        stream_.reset();
    }
}

void QpackDecoderReceiverStream::OnData(std::shared_ptr<IBufferRead> data, bool /*is_last*/, uint32_t error) {
    if (error != 0) {
        LOG_ERROR("QpackDecoderReceiverStream::OnData error: %d", error);
        error_handler_(stream_->GetStreamID(), error);
        return;
    }

    // If buffer is empty (e.g., stream closed with FIN), nothing to do
    if (data->GetDataLength() == 0) {
        LOG_DEBUG("QpackDecoderReceiverStream::OnData: empty buffer, stream likely closed");
        return;
    }

    // Note: UnidentifiedStream has already consumed the stream type byte (0x03 for decoder stream)
    // so we can directly parse the decoder frames
    ParseDecoderFrames(data);
}

void QpackDecoderReceiverStream::ParseDecoderFrames(std::shared_ptr<IBufferRead> data) {
    auto buffer = std::dynamic_pointer_cast<common::IBuffer>(data);
    if (!buffer) {
        LOG_ERROR("QpackDecoderReceiverStream: delivers a non-IBuffer");
        error_handler_(stream_->GetStreamID(), kQpackDecoderStreamError);
        return;
    }

    // P1-H5a: the QUIC stream is a BYTE stream — a decoder instruction may
    // be split across STREAM frames. The pre-fix code parsed the whole
    // buffer at once; a trailing instruction that was only partially
    // present was half-consumed, and every subsequent byte was then parsed
    // from a WRONG offset (e.g. a Section Ack's varint tail re-read as a
    // fresh first byte), silently mis-routing feedback to the encoder.
    //
    // Fix: parse ONE instruction at a time on a zero-copy snapshot
    // (CloneReadable(n, /*move_write_pt=*/false)); commit the consumed
    // prefix on the real buffer only when it fully parses. An unterminated
    // tail stays in the stream receive buffer and resumes naturally on the
    // next OnData. The three RFC 9204 §4.4 patterns cover every possible
    // first byte, so failure means "incomplete or invalid" (indistinguish-
    // able here); the kMaxPendingInstrBytes cap turns a genuinely malformed
    // tail into QPACK_DECODER_STREAM_ERROR, while misdiagnosing an
    // incomplete instruction as malformed would kill a healthy connection.
    while (buffer->GetDataLength() > 0) {
        uint32_t avail = buffer->GetDataLength();
        auto snapshot = buffer->CloneReadable(avail, /*move_write_pt=*/false);
        if (!snapshot) {
            LOG_ERROR("QpackDecoderReceiverStream: snapshot failed");
            error_handler_(stream_->GetStreamID(), kQpackDecoderStreamError);
            return;
        }

        std::vector<std::shared_ptr<IQpackDecoderFrame>> frames;
        if (!DecodeQpackDecoderFrames(snapshot, frames) || frames.empty()) {
            if (avail > kMaxPendingInstrBytes) {
                LOG_ERROR("QpackDecoderReceiverStream: malformed decoder instruction (tail %u bytes)", avail);
                error_handler_(stream_->GetStreamID(), kQpackDecoderStreamError);
                return;
            }
            LOG_DEBUG("QpackDecoderReceiverStream: instruction split across frames, waiting (tail %u bytes)", avail);
            return;  // unterminated tail stays buffered for the next OnData
        }

        // Commit only this instruction's prefix.
        buffer->MoveReadPt(avail - snapshot->GetDataLength());

        for (const auto& frame : frames) {
            if (frame->GetType() == static_cast<uint8_t>(QpackDecoderInstrType::kSectionAck)) {
                QpackSectionAckFrame* f = dynamic_cast<QpackSectionAckFrame*>(frame.get());
                // RFC 9204 §4.4.1: only Stream ID is on the wire. This frame comes
                // from the PEER's decoder and concerns header blocks produced by
                // OUR encoder: retire the earliest outstanding section of that
                // stream there (unpins its dynamic-table entries, advances Known
                // Received Count). It used to be handed to blocked_registry_, which
                // tracks OUR decoder's blocked sections: on a bidirectional stream
                // that could drop or spuriously retry a response section of ours.
                if (local_encoder_) {
                    local_encoder_->OnPeerSectionAck(f->GetStreamId());
                }

            } else if (frame->GetType() == static_cast<uint8_t>(QpackDecoderInstrType::kStreamCancellation)) {
                QpackStreamCancellationFrame* f = dynamic_cast<QpackStreamCancellationFrame*>(frame.get());
                // RFC 9204 §4.4.2: the peer's decoder abandoned the stream; none of
                // our outstanding sections on it will ever be acknowledged. (Same
                // routing fix as Section Ack above.)
                if (local_encoder_) {
                    local_encoder_->OnPeerStreamCancellation(f->GetStreamId());
                }

            } else if (frame->GetType() == static_cast<uint8_t>(QpackDecoderInstrType::kInsertCountInc)) {
                QpackInsertCountIncrementFrame* f = dynamic_cast<QpackInsertCountIncrementFrame*>(frame.get());
                insert_count_ += f->GetDelta();
                // RFC 9204 §4.4.3: Insert Count Increment is sent by the *peer's
                // decoder* to inform our *encoder* that the peer has applied N
                // additional inserts (i.e. its Known Received Count grew by N).
                // Feed it to our encoder so it may start referencing those entries;
                // until it does, the encoder falls back to literal encoding, which
                // is what keeps outbound header blocks from blocking peer streams.
                if (local_encoder_) {
                    local_encoder_->OnPeerInsertCountIncrement(f->GetDelta());
                }
                // It is purely advisory state for our encoder's bookkeeping —
                // it does NOT mean our local decoder gained any new entries,
                // so we MUST NOT call blocked_registry_->NotifyAll() here.
                // The blocked_registry_ tracks *our local decoder's* pending
                // header-block sections waiting for *our local decoder's*
                // dynamic table to grow (which only happens when the peer
                // delivers more Insert instructions on the encoder stream;
                // those are handled in QpackEncoderReceiverStream).
                // The previous spurious NotifyAll() here would invoke every
                // pending retry callback even though our decoder hadn't
                // actually grown, fail the re-decode, and silently drop the
                // section — losing the corresponding header block forever.
            }
        }
    }
}

}  // namespace http3
}  // namespace quicx
