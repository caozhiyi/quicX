
#include "common/buffer/standalone_buffer_chunk.h"
#include "common/buffer/single_block_buffer.h"
#include "test/unit_test/http3/stream/mock_quic_stream.h"

namespace quicx {
namespace quic {

StreamDirection MockQuicStream::GetDirection() {
    return StreamDirection::kBidi;
}

uint64_t MockQuicStream::GetStreamID() {
    return 0;
}

void MockQuicStream::Close() {
    // No-op for mock
}

void MockQuicStream::Reset(uint32_t error) {
    // No-op for mock
}

void MockQuicStream::SetStreamReadCallBack(stream_read_callback cb) {
    read_cb_ = cb;
}

int32_t MockQuicStream::Send(uint8_t* data, uint32_t len) {
    if (write_cb_) {
        write_cb_(len, 0);
    }

    auto peer = peer_.lock();
    if (peer && peer->read_cb_) {
        // Create a standalone readable buffer carrying the payload
        auto chunk = std::make_shared<common::StandaloneBufferChunk>(len);
        auto buf = std::make_shared<common::SingleBlockBuffer>(chunk);
        buf->Write(data, len);
        peer->read_cb_(buf, false, 0);
    }
    return len;
}

int32_t MockQuicStream::Send(std::shared_ptr<IBufferRead> buffer) {
    int len = buffer->GetDataLength();
    if (write_cb_) {
        write_cb_(len, 0);
    }

    auto peer = peer_.lock();
    if (peer && peer->read_cb_) {
        peer->read_cb_(buffer, false, 0);
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
    if (peer && peer->read_cb_) {
        // Create a copy of the buffer data to send to peer
        uint32_t data_len = send_buffer_->GetDataLength();
        auto chunk = std::make_shared<common::StandaloneBufferChunk>(data_len);
        auto copy_buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
        
        // Copy data from send_buffer to copy_buffer using VisitData
        send_buffer_->VisitData([&](uint8_t* data, uint32_t len) {
            copy_buffer->Write(data, len);
            return true;
        });
        
        // Send the copy to peer
        peer->read_cb_(copy_buffer, false, 0);
        
        // Clear the original buffer
        send_buffer_->Clear();
    }
    
    return true;
}

void MockQuicStream::SetStreamWriteCallBack(stream_write_callback cb) {
    write_cb_ = cb;
}

}
}
