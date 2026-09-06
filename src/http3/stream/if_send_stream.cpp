#include "common/buffer/buffer_chunk.h"
#include "common/buffer/buffer_chunk_pool.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/single_block_buffer.h"
#include "common/log/log.h"
#include "common/util/hex.h"

#include "quic/quicx/global_resource.h"

#include "http3/stream/if_send_stream.h"

namespace quicx {
namespace http3 {

bool ISendStream::EnsureStreamPreamble() {
    if (wrote_type_) {
        LOG_DEBUG("ISendStream::EnsureStreamPreamble: already wrote type, stream_id=%llu", stream_->GetStreamID());
        return true;
    }

    // Send stream type for Control Stream (RFC 9114 Section 6.2.1)
    auto buffer = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    LOG_DEBUG("ISendStream::EnsureStreamPreamble: before encoding, stream_id=%llu, stream_type=%u, buffer_length=%u",
        stream_->GetStreamID(), stream_type_, buffer ? buffer->GetDataLength() : 0);

    common::BufferEncodeWrapper wrapper(buffer);
    wrapper.EncodeVarint(static_cast<uint64_t>(stream_type_));

    // Log buffer state after encoding stream type
    if (buffer && buffer->GetDataLength() > 0) {
        auto span = buffer->GetReadableSpan();
        uint32_t log_len = span.GetLength() < 16 ? span.GetLength() : 16;
        LOG_DEBUG(
            "ISendStream::EnsureStreamPreamble: after encoding stream type, buffer length=%u, hex=[%s], stream_id=%llu",
            buffer->GetDataLength(),
            common::BytesToHex(span.GetStart(), log_len, ' ').c_str(), stream_->GetStreamID());
    }

    LOG_DEBUG("ISendStream::EnsureStreamPreamble: sent stream type on stream %llu", stream_->GetStreamID());
    wrote_type_ = true;
    return true;
}

bool ISendStream::EncodeAndAppendControlFrame(const std::function<bool(std::shared_ptr<common::IBuffer>)>& encode) {
    auto chunk = common::BufferChunkPool::Acquire(quic::GlobalResource::Instance().GetThreadLocalBlockPool());
    if (!chunk || !chunk->Valid()) {
        LOG_ERROR("ISendStream::EncodeAndAppendControlFrame: failed to allocate buffer chunk");
        return false;
    }
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    if (!encode(buffer)) {
        return false;
    }
    auto sb = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    if (!sb) {
        LOG_ERROR("ISendStream::EncodeAndAppendControlFrame: stream has no send buffer");
        return false;
    }
    sb->Write(buffer);
    return stream_->Flush();
}

}  // namespace http3
}  // namespace quicx