#include "common/log/log.h"

#include "http3/frame/qpack_decoder_frames.h"
#include "http3/http/error.h"
#include "http3/qpack/blocked_registry.h"
#include "http3/qpack/qpack_decoder.h"
#include "http3/stream/qpack_encoder_receiver_stream.h"

namespace quicx {
namespace http3 {

QpackEncoderReceiverStream::QpackEncoderReceiverStream(const std::shared_ptr<IQuicRecvStream>& stream,
    const std::shared_ptr<QpackDecoder>& qpack_decoder, const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler):
    IRecvStream(StreamType::kQpackEncoder, stream, error_handler),
    qpack_decoder_(qpack_decoder),
    blocked_registry_(blocked_registry) {
    stream_->SetStreamReadCallBack([this](auto a, auto b, auto c) { OnData(a, b, c); });

    LOG_DEBUG("QpackEncoderReceiverStream created for stream %llu", stream_->GetStreamID());
}

QpackEncoderReceiverStream::~QpackEncoderReceiverStream() {
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

void QpackEncoderReceiverStream::OnData(std::shared_ptr<IBufferRead> data, bool /*is_last*/, uint32_t error) {
    if (error != 0) {
        LOG_ERROR("QpackEncoderReceiverStream::OnData error: %d on stream %llu", error, stream_->GetStreamID());
        error_handler_(stream_->GetStreamID(), error);
        return;
    }

    if (data->GetDataLength() == 0) {
        return;
    }

    LOG_DEBUG(
        "QpackEncoderReceiverStream: received %u bytes on stream %llu", data->GetDataLength(), stream_->GetStreamID());

    // Parse QPACK encoder instructions
    ParseEncoderInstructions(data);
}

void QpackEncoderReceiverStream::ParseEncoderInstructions(std::shared_ptr<IBufferRead> data) {
    // RFC 9204 Section 4.3: Encoder instructions include:
    // - Set Dynamic Table Capacity
    // - Insert With Name Reference
    // - Insert Without Name Reference
    // - Duplicate

    auto buffer = std::dynamic_pointer_cast<common::IBuffer>(data);
    if (!buffer) {
        LOG_ERROR("QpackEncoderReceiverStream: stream %llu delivers a non-IBuffer", stream_->GetStreamID());
        error_handler_(stream_->GetStreamID(), kQpackEncoderStreamError);
        return;
    }

    // Track insert count before processing to determine delta
    uint64_t insert_count_before = qpack_decoder_->GetInsertCount();
    bool parsed_any = false;

    // P1-H5a: the QUIC stream is a BYTE stream — an instruction may be
    // split across STREAM frames. The pre-fix code handed the whole buffer
    // to DecodeEncoderInstructions, which consumed past the boundary of the
    // last (incomplete) instruction; the remaining bytes were then parsed
    // from a WRONG offset on the next OnData, silently corrupting every
    // subsequent dynamic-table operation.
    //
    // Fix: parse ONE instruction at a time on a zero-copy snapshot
    // (CloneReadable(n, /*move_write_pt=*/false)). On success commit the
    // consumed prefix on the real buffer; on failure the real buffer is
    // untouched and the unterminated tail stays in the stream receive
    // buffer, so the next OnData naturally resumes from the instruction
    // boundary. A tail that grows past kMaxPendingInstrBytes without ever
    // completing is a malformed/oversized declaration and errors the stream
    // (RFC 9204: an encoder-stream error MUST be treated as a connection
    // error of type QPACK_ENCODER_STREAM_ERROR).
    while (buffer->GetDataLength() > 0) {
        uint32_t avail = buffer->GetDataLength();
        auto snapshot = buffer->CloneReadable(avail, /*move_write_pt=*/false);
        if (!snapshot) {
            LOG_ERROR("QpackEncoderReceiverStream: snapshot failed on stream %llu", stream_->GetStreamID());
            error_handler_(stream_->GetStreamID(), kQpackEncoderStreamError);
            return;
        }

        if (!qpack_decoder_->DecodeOneEncoderInstruction(snapshot)) {
            // Failure means "incomplete" OR "semantically invalid" — the
            // two are indistinguishable from the caller side, and the four
            // RFC 9204 §4.3 patterns cover every possible first byte, so
            // there is no "unknown opcode" fast path either. Treat both as
            // wait-for-more-tail and let the kMaxPendingInstrBytes cap turn
            // a genuinely malformed tail into an error; misdiagnosing an
            // incomplete instruction as malformed would kill a healthy
            // connection, while the delayed error only postpones the
            // verdict. (Instruction decode is side-effect free until the
            // final Add/Set succeeds, so a failed trial leaves the dynamic
            // table untouched.)
            if (avail > kMaxPendingInstrBytes) {
                LOG_ERROR(
                    "QpackEncoderReceiverStream: malformed encoder instruction on stream %llu (tail %u bytes)",
                    stream_->GetStreamID(), avail);
                error_handler_(stream_->GetStreamID(), kQpackEncoderStreamError);
                return;
            }
            LOG_DEBUG(
                "QpackEncoderReceiverStream: instruction split across frames on stream %llu, waiting (tail %u bytes)",
                stream_->GetStreamID(), avail);
            return;  // unterminated tail stays buffered for the next OnData
        }

        // Commit only this instruction's prefix.
        buffer->MoveReadPt(avail - snapshot->GetDataLength());
        parsed_any = true;
    }

    uint64_t insert_count_after = qpack_decoder_->GetInsertCount();
    uint64_t delta = insert_count_after - insert_count_before;

    LOG_DEBUG("QpackEncoderReceiverStream: successfully parsed encoder instructions on stream %llu (delta=%llu)",
        stream_->GetStreamID(), delta);

    if (!parsed_any) {
        return;
    }

    // Notify any blocked streams that new table entries are available
    if (blocked_registry_) {
        blocked_registry_->NotifyAll();
    }

    // RFC 9204 §4.4.3: After processing encoder instructions that add new entries,
    // the decoder SHOULD emit Insert Count Increment to inform the peer's encoder
    // that these entries are now available for reference in header blocks.
    if (delta > 0) {
        qpack_decoder_->EmitDecoderFeedback(static_cast<uint8_t>(QpackDecoderInstrType::kInsertCountInc), delta);
    }
}

}  // namespace http3
}  // namespace quicx
