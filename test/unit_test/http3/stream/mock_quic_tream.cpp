
#include "common/buffer/standalone_buffer_chunk.h"
#include "common/buffer/single_block_buffer.h"
#include "test/unit_test/http3/stream/mock_quic_stream.h"

namespace quicx {
namespace quic {

StreamDirection MockQuicStream::GetDirection() {
    return direction_;
}

uint64_t MockQuicStream::GetStreamID() {
    return stream_id_;
}

void MockQuicStream::Close() {
    // No-op for mock
}

void MockQuicStream::Reset(uint32_t error) {
    // No-op for mock
}

void MockQuicStream::SetStreamReadCallBack(stream_read_callback cb) {
    read_cb_ = cb;
    // Drain anything that the peer pushed at us before this callback
    // existed. Real QUIC would buffer pre-acceptance data inside the
    // recv-stream object; we mirror that here so HTTP/3 streams that get
    // adopted out-of-order (e.g. unidirectional QPACK / control streams
    // whose first byte arrives before the receiver creates its
    // RecvStream wrapper) don't lose their stream-type preamble.
    if (read_cb_ && !pending_inbound_data_.empty()) {
        std::vector<std::pair<std::shared_ptr<IBufferRead>, bool>> drain;
        drain.swap(pending_inbound_data_);
        for (auto& kv : drain) {
            read_cb_(kv.first, kv.second, 0);
        }
    }
}

namespace {
// Helper: deliver a buffer to a peer stream, buffering when no read_cb_ is
// installed yet. Same semantics for both Send variants and Flush.
inline void DeliverToPeer(std::shared_ptr<MockQuicStream> peer,
                          std::shared_ptr<IBufferRead> buf, bool is_last) {
    if (!peer) return;
    if (peer->ReadCb()) {
        peer->ReadCb()(buf, is_last, 0);
    } else {
        peer->QueuePendingInbound(buf, is_last);
    }
}
}  // namespace

int32_t MockQuicStream::Send(uint8_t* data, uint32_t len) {
    if (write_cb_) {
        write_cb_(len, 0);
    }

    auto peer = peer_.lock();
    if (peer) {
        // Create a standalone readable buffer carrying the payload
        auto chunk = std::make_shared<common::StandaloneBufferChunk>(len);
        auto buf = std::make_shared<common::SingleBlockBuffer>(chunk);
        buf->Write(data, len);
        DeliverToPeer(peer, buf, false);
    }
    return len;
}

int32_t MockQuicStream::Send(std::shared_ptr<IBufferRead> buffer) {
    int len = buffer->GetDataLength();
    if (write_cb_) {
        write_cb_(len, 0);
    }

    auto peer = peer_.lock();
    if (peer) {
        DeliverToPeer(peer, buffer, false);
    }
    return len;
}

std::shared_ptr<IBufferWrite> MockQuicStream::GetSendBuffer() {
    if (!send_buffer_) {
        // Create a buffer with reasonable capacity for testing
        auto chunk = std::make_shared<common::StandaloneBufferChunk>(64 * 1024);
        send_buffer_ = std::make_shared<common::SingleBlockBuffer>(chunk);
    }
    return send_buffer_;
}

bool MockQuicStream::Flush() {
    if (!send_buffer_ || send_buffer_->GetDataLength() == 0) {
        return true;
    }

    auto peer = peer_.lock();
    if (peer) {
        // Snapshot the pending data into a fresh readable buffer so that
        // delivery (or buffering) is independent of our send_buffer_ state.
        uint32_t data_len = send_buffer_->GetDataLength();
        auto chunk = std::make_shared<common::StandaloneBufferChunk>(data_len);
        auto copy_buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
        send_buffer_->VisitData([&](uint8_t* data, uint32_t len) {
            copy_buffer->Write(data, len);
            return true;
        });

        // Use the same pre-callback buffering semantics as Send(), so that
        // unidirectional preambles (control SETTINGS, QPACK encoder/decoder
        // stream type bytes) flushed before the peer installs its read
        // callback are not silently dropped.
        DeliverToPeer(peer, copy_buffer, false);

        send_buffer_->Clear();
    }

    return true;
}

void MockQuicStream::SetStreamWriteCallBack(stream_write_callback cb) {
    write_cb_ = cb;
}

uint64_t MockQuicStream::GetPendingSendBytes() {
    if (!send_buffer_) {
        return 0;
    }
    return send_buffer_->GetDataLength();
}

}
}
