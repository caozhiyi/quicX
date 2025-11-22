// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_IF_BUFFER
#define COMMON_BUFFER_IF_BUFFER

#include <cstdint>
#include <string>
#include "common/buffer/buffer_span.h"
#include "common/include/if_buffer_read.h"
#include "common/buffer/buffer_read_view.h"
#include "common/include/if_buffer_write.h"
#include "common/buffer/shared_buffer_span.h"

namespace quicx {
namespace common {

// Extended buffer interface used throughout the buffernew infrastructure. It
// builds on top of IBufferRead by adding write-side APIs as well as helpers to
// expose the underlying memory in different forms (views/spans). Concrete
// implementations are expected to wrap BufferChunk-derived objects.
class IBuffer: public virtual IBufferRead, public virtual IBufferWrite {
public:
    IBuffer() {}
    virtual ~IBuffer() {}

    // Read the available data into |data| without advancing the read pointer.
    virtual uint32_t ReadNotMovePt(uint8_t* data, uint32_t len) = 0;
    // Move the read pointer by |len| bytes (positive to consume, negative to
    // rewind). Implementations should clamp to the readable range.
    virtual uint32_t MoveReadPt(uint32_t len) = 0;
    // Read the available data into |data| and advance the read pointer.
    virtual uint32_t Read(uint8_t* data, uint32_t len) = 0;
    // Visit highlighted spans of readable data without modifying internal
    // pointers. Each invocation provides a pointer/length pair referencing a
    // contiguous portion of the buffer.
    virtual void VisitData(const std::function<bool(uint8_t*, uint32_t)>& visitor) = 0;
    // Return the number of bytes currently readable.
    virtual uint32_t GetDataLength() = 0;
    // Reset the buffer to an empty state.
    virtual void Clear() = 0;

    // *************************** inner interfaces ***************************
    // Clone a readable portion of the buffer (helper for frame decoding).
    // Returns a new buffer that references [current_read_pos, current_read_pos + length).
    // The original buffer's read pointer is advanced by length.
    // This is useful for protocols that need to extract a fixed-length payload
    // while keeping the rest for further processing.
    virtual std::shared_ptr<IBuffer> CloneReadable(uint32_t length, bool move_write_pt = true) = 0;
    // set the read limit of the buffer
    // virtual void SetReadableLimit(uint32_t limit) = 0;
    // Return a read-only view over the readable data.
    virtual BufferReadView GetReadView() const = 0;
    // Return a non-owning span describing the readable data.
    virtual BufferSpan GetReadableSpan() const = 0;
    // Return a shared span that keeps the chunk alive.
    // @param length: 0 means all available data
    // @param must_fill_length: if true, returns invalid span when available < length
    virtual SharedBufferSpan GetSharedReadableSpan(uint32_t length = 0, bool must_fill_length = false) const = 0;
    // Return the data as a string.
    virtual std::string GetDataAsString() = 0;
    virtual void VisitDataSpans(const std::function<bool(SharedBufferSpan&)>& visitor) = 0;

    // Append |len| bytes from |data| into the buffer.
    virtual uint32_t Write(const uint8_t* data, uint32_t len) = 0;
    // Write helpers enqueue externally managed spans. The overload with
    // data_len allows callers to cap the exposed readable portion (useful when
    // only a prefix of the span contains meaningful data).
    virtual uint32_t Write(const SharedBufferSpan& span) = 0;
    // Write the data from the buffer into the current buffer.
    virtual uint32_t Write(std::shared_ptr<IBuffer> buffer) = 0;
    // Write the data from the buffer into the current buffer, with the length of the data.
    virtual uint32_t Write(const SharedBufferSpan& span, uint32_t data_len) = 0;
    // return the remaining length that can be written
    virtual uint32_t GetFreeLength() = 0;
    // return the length of the data actually move
    virtual uint32_t MoveWritePt(uint32_t len) = 0;
    // return buffer write and end pos
    virtual BufferSpan GetWritableSpan() = 0;
    virtual BufferSpan GetWritableSpan(uint32_t expected_length) = 0;
    // return the chunk of the buffer
    virtual std::shared_ptr<IBufferChunk> GetChunk() const = 0;
};

}  // namespace common
}  // namespace quicx

#endif