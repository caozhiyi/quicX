// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_IF_BUFFER_WRITE
#define COMMON_BUFFER_IF_BUFFER_WRITE

#include <cstdint>

namespace quicx {

/**
 * @brief Interface for writing data to buffers
 *
 * This interface provides write operations that append data and manage the write pointer.
 */
class IBufferWrite {
public:
    IBufferWrite() {}
    virtual ~IBufferWrite() {}

    /**
     * @brief Write data to the buffer
     *
     * Copies data into the writable region and advances the write pointer.
     * The actual copied length is capped by remaining capacity.
     *
     * @param data Source buffer to copy data from
     * @param len Number of bytes to write
     * @return Number of bytes actually written
     */
    virtual uint32_t Write(const uint8_t* data, uint32_t len) = 0;

    /**
     * @brief Move the write pointer
     *
     * Moves the write pointer without touching memory. Positive values advance
     * the pointer, negative values rewind it. The movement is clamped to the
     * writable range.
     *
     * @param len Number of bytes to move (positive or negative)
     * @return Actual distance moved
     */
    virtual uint32_t MoveWritePt(uint32_t len) = 0;
};

}

#endif