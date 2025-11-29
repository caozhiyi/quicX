#include <cstring>

#include "common/buffer/multi_block_buffer_decode_wrapper.h"

namespace quicx {
namespace common {

MultiBlockBufferDecodeWrapper::MultiBlockBufferDecodeWrapper(std::shared_ptr<IBuffer> buffer):
    buffer_(buffer),
    read_view_(buffer),
    flushed_(false) {
}

MultiBlockBufferDecodeWrapper::~MultiBlockBufferDecodeWrapper() {
    if (!flushed_) {
        Flush();
    }
}

void MultiBlockBufferDecodeWrapper::Flush() {
    flushed_ = true;
    // Sync the read view to advance the underlying buffer's read pointer
    read_view_.Sync();
}

void MultiBlockBufferDecodeWrapper::CancelDecode() {
    flushed_ = true;
    // Reset the read view without syncing
    read_view_.Reset(buffer_);
}

bool MultiBlockBufferDecodeWrapper::ReadForDecode(uint8_t* temp_buf, uint32_t needed_len, uint32_t& actual_len) {
    // Use the read view to read data without modifying the underlying buffer's read pointer
    actual_len = read_view_.ReadNotMovePt(temp_buf, needed_len);
    return actual_len >= needed_len;
}

bool MultiBlockBufferDecodeWrapper::DecodeFixedUint8(uint8_t& value) {
    uint8_t temp_buf[1];
    uint32_t actual_len = 0;
    
    if (!ReadForDecode(temp_buf, 1, actual_len) || actual_len < 1) {
        return false;
    }
    
    uint8_t* decoded_end = common::FixedDecodeUint8(temp_buf, temp_buf + 1, value);
    if (decoded_end == nullptr) {
        return false;
    }
    
    read_view_.MoveReadPt(1);
    flushed_ = false;
    return true;
}

bool MultiBlockBufferDecodeWrapper::DecodeFixedUint16(uint16_t& value) {
    uint8_t temp_buf[2];
    uint32_t actual_len = 0;
    
    if (!ReadForDecode(temp_buf, 2, actual_len) || actual_len < 2) {
        return false;
    }
    
    uint8_t* decoded_end = common::FixedDecodeUint16(temp_buf, temp_buf + 2, value);
    if (decoded_end == nullptr) {
        return false;
    }
    
    read_view_.MoveReadPt(2);
    flushed_ = false;
    return true;
}

bool MultiBlockBufferDecodeWrapper::DecodeFixedUint32(uint32_t& value) {
    uint8_t temp_buf[4];
    uint32_t actual_len = 0;
    
    if (!ReadForDecode(temp_buf, 4, actual_len) || actual_len < 4) {
        return false;
    }
    
    uint8_t* decoded_end = common::FixedDecodeUint32(temp_buf, temp_buf + 4, value);
    if (decoded_end == nullptr) {
        return false;
    }
    
    read_view_.MoveReadPt(4);
    flushed_ = false;
    return true;
}

bool MultiBlockBufferDecodeWrapper::DecodeFixedUint64(uint64_t& value) {
    uint8_t temp_buf[8];
    uint32_t actual_len = 0;
    
    if (!ReadForDecode(temp_buf, 8, actual_len) || actual_len < 8) {
        return false;
    }
    
    uint8_t* decoded_end = common::FixedDecodeUint64(temp_buf, temp_buf + 8, value);
    if (decoded_end == nullptr) {
        return false;
    }
    
    read_view_.MoveReadPt(8);
    flushed_ = false;
    return true;
}

common::BufferSpan MultiBlockBufferDecodeWrapper::GetDataSpan() const {
    // This is tricky for multi-block buffers
    // We can't easily return a span that spans multiple blocks
    // For now, return an empty span
    return common::BufferSpan();
}

uint32_t MultiBlockBufferDecodeWrapper::GetDataLength() const {
    return read_view_.GetDataLength();
}

uint32_t MultiBlockBufferDecodeWrapper::GetReadLength() const {
    return GetConsumedOffset();
}

uint32_t MultiBlockBufferDecodeWrapper::GetConsumedOffset() const {
    return read_view_.GetReadOffset();
}

std::shared_ptr<IBuffer> MultiBlockBufferDecodeWrapper::GetBuffer() const {
    return buffer_;
}

}  // namespace common
}  // namespace quicx

