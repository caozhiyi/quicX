#ifndef COMMON_BUFFER_MULTI_BLOCK_BUFFER_READ_VIEW
#define COMMON_BUFFER_MULTI_BLOCK_BUFFER_READ_VIEW

#include <cstdint>
#include <functional>
#include <memory>

#include "common/buffer/if_buffer.h"
#include "common/include/if_buffer_read.h"

namespace quicx {
namespace common {

// MultiBlockBufferReadView provides read-only operations over an IBuffer with
// an independent read pointer. Unlike BufferReadView which operates on a
// contiguous memory range, this view can handle multi-block buffers by
// delegating to the underlying IBuffer's ReadNotMovePt and MoveReadPt methods.
// The view maintains its own read offset without modifying the underlying
// buffer's read pointer, allowing multiple views to read from the same buffer
// independently.
class MultiBlockBufferReadView: public virtual IBufferRead {
public:
    MultiBlockBufferReadView();
    // Construct a view over an IBuffer, starting at the current read position.
    explicit MultiBlockBufferReadView(std::shared_ptr<IBuffer> buffer);
    
    // Reset the view to a new buffer, starting from its current read position.
    void Reset(std::shared_ptr<IBuffer> buffer);
    
    // Read APIs mirror the semantics of IBufferRead. The view maintains its
    // own read offset, so these operations don't affect the underlying buffer's
    // read pointer until Sync() is called.
    uint32_t ReadNotMovePt(uint8_t* data, uint32_t len) override;
    uint32_t MoveReadPt(uint32_t len) override;
    uint32_t Read(uint8_t* data, uint32_t len) override;
    void VisitData(const std::function<bool(uint8_t*, uint32_t)>& visitor) override;
    
    // Query helpers.
    uint32_t GetDataLength() override;
    uint32_t GetDataLength() const;
    void Clear() override;
    
    // Sync the underlying buffer's read pointer to match this view's position.
    // After syncing, the underlying buffer's read pointer will be advanced by
    // the view's read offset, and the view's offset will be reset to 0.
    void Sync();
    
    // Get the current read offset (bytes read but not yet synced).
    uint32_t GetReadOffset() const;
    
    // Quick validity check.
    bool Valid() const;

private:
    std::shared_ptr<IBuffer> buffer_;
    uint32_t read_offset_;  // Offset from the buffer's read position when view was created
};

}  // namespace common
}  // namespace quicx

#endif

