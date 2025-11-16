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

    other.data_ = nullptr;
    other.length_ = 0;
}

StandaloneBufferChunk& StandaloneBufferChunk::operator=(StandaloneBufferChunk&& other) noexcept {
    if (this != &other) {
        Release();

        data_ = other.data_;
        length_ = other.length_;

        other.data_ = nullptr;
        other.length_ = 0;
    }
    return *this;
}

void StandaloneBufferChunk::Release() {
    delete[] data_;
    data_ = nullptr;
    length_ = 0;
}

}
}


