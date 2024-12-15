#include <cstring>
#include <cstdlib> // for abort
#include "common/buffer/buffer.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_read_view.h"
#include "common/buffer/buffer_write_view.h"

namespace quicx {
namespace common {

Buffer::Buffer(BufferSpan& span):
    can_read_(false),
    IBuffer(nullptr) {
    buffer_start_ = span.GetStart();
    buffer_end_ = span.GetEnd();
    read_pos_ = write_pos_ = buffer_start_;
}

Buffer::Buffer(uint8_t* start, uint32_t len):
    can_read_(false),
    IBuffer(nullptr) {
    buffer_start_ = start;
    buffer_end_ = start + len;
    read_pos_ = write_pos_ = buffer_start_;
}

Buffer::Buffer(uint8_t* start, uint8_t* end):
    can_read_(false),
    IBuffer(nullptr) {
    buffer_start_ = start;
    buffer_end_ = end;
    read_pos_ = write_pos_ = buffer_start_;
}

Buffer::Buffer(std::shared_ptr<common::BlockMemoryPool>& alloter):
    can_read_(false),
    IBuffer(alloter) {

    buffer_start_ = (uint8_t*)alloter->PoolLargeMalloc();
    buffer_end_ = buffer_start_ + alloter->GetBlockLength();
    read_pos_ = write_pos_ = buffer_start_;
}

Buffer::~Buffer() {
    if (buffer_start_ != nullptr) {
        auto alloter = alloter_.lock();
        if (alloter) {
            void* m = (void*)buffer_start_;
            alloter->PoolLargeFree(m);
        }
    }
}

uint32_t Buffer::ReadNotMovePt(uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        return 0;
    }
    return InnerRead(data, len, false);
}

uint32_t Buffer::MoveReadPt(int32_t len) {
    if (!buffer_start_) {
        return 0;
    }

    if (len > 0) {
        if (read_pos_ <= write_pos_) {
            size_t size = write_pos_ - read_pos_;
            // all buffer will be used
            if ((int32_t)size <= len) {
                Clear();
                return (int32_t)size;

            // part of buffer will be used
            } else {
                read_pos_ += len;
                return len;
            }

        } else {
            // shouldn't be here
            abort();
            return 0;
        }

    } else {
        len = -len;
        if (buffer_start_ <= read_pos_) {
            size_t size = read_pos_ - buffer_start_;
            // reread all buffer
            if ((int32_t)size <= len) {
                read_pos_ -= size;
                return (int32_t)size;

            // only reread part of buffer
            } else {
                read_pos_ -= len;
                return len;
            }

        } else {
            // shouldn't be here
            abort();
            return 0;
        }
    }
}

uint32_t Buffer::Read(uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        return 0;
    }
    return InnerRead(data, len, true);
}

uint32_t Buffer::GetDataLength() {
    return uint32_t(write_pos_ - read_pos_); 
}

BufferSpan Buffer::GetReadSpan() {
    return std::move(BufferSpan(read_pos_, write_pos_));
}

BufferReadView Buffer::GetReadView(uint32_t offset) {
    return std::move(BufferReadView(read_pos_, write_pos_));
}

std::shared_ptr<common::IBufferRead> Buffer::GetReadViewPtr(uint32_t offset) {
    return std::make_shared<BufferReadView>(read_pos_, write_pos_);
}

uint8_t* Buffer::GetData() {
    return read_pos_;
}

// clear all data
void Buffer::Clear() {
    write_pos_ = read_pos_ = buffer_start_;
    can_read_ = false;
}

uint32_t Buffer::Write(uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        return 0;
    }
    return InnerWrite((uint8_t*)data, len);
}

uint32_t Buffer::GetFreeLength() {
    return uint32_t(buffer_end_ - write_pos_);
}

uint32_t Buffer::MoveWritePt(int32_t len) {
    if (!buffer_start_) {
        return 0;
    }

    if (len > 0) {
        if (write_pos_ <= buffer_end_) {
            size_t size = buffer_end_ - write_pos_;
            // all buffer will be used
            if ((int32_t)size <= len) {
                write_pos_ += size;
                can_read_ = true;
                return (int32_t)size;

            // part of buffer will be used
            } else {
                write_pos_ += len;
                return len;
            }

        } else {
            // shouldn't be here
            abort();
            return 0;
        }

    } else {
        len = -len;
        if (read_pos_ <= write_pos_) {
            size_t size = write_pos_ - read_pos_;
            // rewrite all buffer
            if ((int32_t)size <= len) {
                Clear();
                return (int32_t)size;

            // only rewrite part of buffer
            } else {
                write_pos_ -= len;
                return len;
            }
        
        } else {
            // shouldn't be here
            abort();
            return 0;
        }
    }
}

BufferSpan Buffer::GetWriteSpan() {
    return std::move(BufferSpan(write_pos_, buffer_end_));
}

BufferWriteView Buffer::GetWriteView(uint32_t offset) {
    return std::move(BufferWriteView(write_pos_, buffer_end_));
}

std::shared_ptr<common::IBufferWrite> Buffer::GetWriteViewPtr(uint32_t offset) {
    return std::make_shared<BufferWriteView>(write_pos_, buffer_end_);
}

uint32_t Buffer::InnerRead(uint8_t* data, uint32_t len, bool move_pt) {
    /*s-----------r-----w-------------e*/
    if (read_pos_ <= write_pos_) {
        size_t size = write_pos_ - read_pos_;
        // res can load all
        if (size <= len) {
            memcpy(data, read_pos_, size);
            if(move_pt) {
                Clear();
            }
            return (uint32_t)size;

        // only read len
        } else {
            memcpy(data, read_pos_, len);
            if(move_pt) {
                read_pos_ += len;
            }
            return len;
        }

    } else {
        // shouldn't be here
        abort();
        return 0;
    }
}

uint32_t Buffer::InnerWrite(uint8_t* data, uint32_t len) {
    if (write_pos_ <= buffer_end_) {
        size_t size = buffer_end_ - write_pos_;
        // all buffer will be used
        if (size <= len) {
            memcpy(write_pos_, data, size);

            write_pos_ += size;
            can_read_ = true;
            return (int32_t)size;

        // part of buffer will be used
        } else {
            memcpy(write_pos_, data, len);
            write_pos_ += len;
            return len;
        }
    } else {
        // shouldn't be here
        abort();
        return 0;
    }
}

}
}