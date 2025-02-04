#include "common/buffer/buffer.h"
#include "common/buffer/buffer_chains.h"

namespace quicx {
namespace common {

BufferChains::BufferChains(std::shared_ptr<common::BlockMemoryPool>& alloter):
    read_pos_(nullptr),
    write_pos_(nullptr),
    alloter_(alloter) {

}

BufferChains::~BufferChains() {

}

uint32_t BufferChains::ReadNotMovePt(uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        return 0;
    }
    
    uint32_t size = 0;
    for (auto iter = read_pos_; iter && iter->GetDataLength() > 0; iter = iter->GetNext()) {
        size += iter->ReadNotMovePt(data + size, len - size);
        if (size >= len) {
            break;
        }
    }
    return size;
}

int32_t BufferChains::MoveReadPt(int32_t len) {
    int32_t size = 0;
    for (; read_pos_ && read_pos_->GetDataLength() > 0; read_pos_ = read_pos_->GetNext()) {
        size += read_pos_->MoveReadPt(len - size);
        if (size >= len) {
            break;
        }
        buffer_list_.PopFront();
    }
    if (buffer_list_.Size() == 0) {
        Clear();
    }

    return size;
}

uint32_t BufferChains::Read(uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        return 0;
    }

    uint32_t size = 0;
    for (; read_pos_ && read_pos_->GetDataLength() > 0; read_pos_ = read_pos_->GetNext()) {
        size += read_pos_->Read(data + size, len - size);
        if (size >= len) {
            break;
        }
        buffer_list_.PopFront();
    }
    if (buffer_list_.Size() == 0) {
        Clear();
    }
    return size;
}

uint32_t BufferChains::GetDataLength() {
    uint32_t size = 0;
    for (auto iter = read_pos_; iter && iter->GetDataLength() > 0; iter = iter->GetNext()) {
        size += iter->GetDataLength();
    }
    return size;
}

std::shared_ptr<BufferBlock> BufferChains::GetReadBuffers() {
    return read_pos_;
}

uint32_t BufferChains::Write(uint8_t* data, uint32_t len) {
    uint32_t offset = 0;
    while (offset < len) {
        if (!write_pos_ || write_pos_->GetFreeLength() == 0) {
            write_pos_ = std::make_shared<BufferBlock>(alloter_);
            buffer_list_.PushBack(write_pos_);
            if (!read_pos_) {
                read_pos_ = write_pos_;
            }
        }
        offset += write_pos_->Write(data + offset, len - offset);
    }
    return offset;
}

uint32_t BufferChains::GetFreeLength() {
    uint32_t size = 0;
    for (auto iter = write_pos_; iter; iter = iter->GetNext()) {
        size += iter->GetFreeLength();
    }
    return size;
}

int32_t BufferChains::MoveWritePt(int32_t len) {
    int32_t size = 0;
    for (; write_pos_ && write_pos_->GetFreeLength() > 0; write_pos_ = write_pos_->GetNext()) {
        size += write_pos_->MoveWritePt(len - size);
        if (size >= len) {
            break;
        }
    }
    return size;
}

std::shared_ptr<BufferBlock> BufferChains::GetWriteBuffers(uint32_t len) {
    uint32_t offset = 0;
    std::shared_ptr<BufferBlock> cur_write = write_pos_;
    while (offset < len) {
        if (!cur_write || cur_write->GetFreeLength() == 0) {
            cur_write = std::make_shared<BufferBlock>(alloter_);
            buffer_list_.PushBack(cur_write);
            if (!write_pos_) {
                write_pos_ = cur_write;
            }
        }
        offset += cur_write->GetFreeLength();
        cur_write = cur_write->GetNext();
    }
    return write_pos_;
}

void BufferChains::Clear() {
    read_pos_ = nullptr;
    write_pos_ = nullptr;
}

}
}
