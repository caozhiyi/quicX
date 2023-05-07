// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_FIX_SORT_BUFFER
#define COMMON_BUFFER_FIX_SORT_BUFFER

#include "common/buffer/buffer_sort_chains_interface.h"

namespace quicx {

// fix length buffer cache
class FixSortBuffer:
    public IBufferSortChains {
public:
    FixSortBuffer(uint32_t len);
    ~FixSortBuffer();

    // read to data buf but don't change the read point
    // return the length of the data actually read
    virtual uint32_t ReadNotMovePt(uint8_t* data, uint32_t len);
    // move read point
    // return the length of the data actually move
    virtual uint32_t MoveReadPt(int32_t len);
    // return the length of the data actually read
    virtual uint32_t Read(uint8_t* data, uint32_t len);
    // return remaining length of readable data
    virtual uint32_t GetDataLength();
    // return the length of the actual write
    virtual uint32_t Write(uint64_t offset, uint8_t* data, uint32_t len);

protected:
    uint32_t Read(uint8_t* data, uint32_t len, bool move_pt);
    uint32_t Write(const char* data, uint32_t len);
    // clear all data
    void Clear();
private:
    uint8_t* _buffer_start;
    uint8_t* _buffer_end;

    uint8_t* _read_pos;
    uint8_t* _write_pos;
    bool _can_read;         //when _read == _write? Is there any data can be read.
};

}

#endif