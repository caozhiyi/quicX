// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_READONLY
#define COMMON_BUFFER_BUFFER_READONLY

#include <memory>
#include "common/buffer/buffer_writer.h"
#include "common/buffer/buffer_interface.h"

namespace quicx {

class BlockMemoryPool;
// Single block cyclic cache
class BufferWriteOnly: 
    public IBufferWriteOnly,
    public BufferWriter {

public:
    BufferWriteOnly(std::shared_ptr<BlockMemoryPool>& IAlloter);
    ~BufferWriteOnly();

    virtual uint32_t Write(const char* data, uint32_t len);

    virtual uint32_t GetCanWriteLength();
};

}

#endif