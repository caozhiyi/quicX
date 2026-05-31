#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace common {

StandaloneBufferChunk::StandaloneBufferChunk(uint32_t size) {
    if (size == 0) {
        return;
    }
    data_ = new uint8_t[size];
    length_ = size;
}

StandaloneBufferChunk::~StandaloneBufferChunk() {
    Release();
}

StandaloneBufferChunk::StandaloneBufferChunk(StandaloneBufferChunk&& other) noexcept {
    data_ = other.data_;
    length_ = other.length_;
    write_floor_offset_ = other.write_floor_offset_;
    freeze_count_ = other.freeze_count_;
    other.data_ = nullptr;
    other.length_ = 0;
    other.write_floor_offset_ = 0;
    other.freeze_count_ = 0;
}

StandaloneBufferChunk& StandaloneBufferChunk::operator=(StandaloneBufferChunk&& other) noexcept {
    if (this != &other) {
        Release();

        data_ = other.data_;
        length_ = other.length_;
        write_floor_offset_ = other.write_floor_offset_;
        freeze_count_ = other.freeze_count_;
        other.data_ = nullptr;
        other.length_ = 0;
        other.write_floor_offset_ = 0;
        other.freeze_count_ = 0;
    }
    return *this;
}

void StandaloneBufferChunk::Release() {
    delete[] data_;
    data_ = nullptr;
    length_ = 0;
    write_floor_offset_ = 0;
    freeze_count_ = 0;
}

// ----- Zero-copy invariant 1 (write-floor / freeze) ----------------------

void StandaloneBufferChunk::FreezeUpTo(uint8_t* end) {
    if (!data_ || end == nullptr) {
        return;
    }
    if (end <= data_) {
        ++freeze_count_;
        return;
    }
    uint32_t offset = static_cast<uint32_t>(end - data_);
    if (offset > length_) {
        offset = length_;
    }
    if (offset > write_floor_offset_) {
        write_floor_offset_ = offset;
    }
    ++freeze_count_;
}

void StandaloneBufferChunk::Unfreeze(uint8_t* /*end*/) {
    if (freeze_count_ == 0) {
        return;
    }
    --freeze_count_;
    if (freeze_count_ == 0) {
        write_floor_offset_ = 0;
    }
}

uint8_t* StandaloneBufferChunk::GetWriteFloor() const {
    if (!data_) {
        return nullptr;
    }
    return data_ + write_floor_offset_;
}

}
}


