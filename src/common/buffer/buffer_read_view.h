#ifndef COMMON_BUFFERNEW_BUFFER_READ_VIEW
#define COMMON_BUFFERNEW_BUFFER_READ_VIEW

#include <cstdint>
#include <functional>
#include "common/buffer/buffer_span.h"
#include "common/include/if_buffer_read.h"

namespace quicx {
namespace common {

// BufferReadView provides read-only operations over an externally managed
// memory range. It is intentionally lightweight: no heap allocations, no
// reference counting. The caller promises that the backing storage remains
// valid for the view's entire lifetime.
class BufferReadView:
    public virtual IBufferRead {
public:
    BufferReadView();
    // Convenience constructors that immediately point the view to a span.
    BufferReadView(uint8_t* start, uint32_t len);
    BufferReadView(uint8_t* start, uint8_t* end);

    // Reset the view to a new span. Invalid ranges are rejected and logged,
    // leaving the view empty.
    void Reset(uint8_t* start, uint32_t len);
    void Reset(uint8_t* start, uint8_t* end);

    // Read APIs mirror the semantics of the legacy buffer interface. len is
    // capped by the amount of readable data. ReadNotMovePt() leaves the read
    // pointer untouched; Read() advances it; MoveReadPt() moves without copying.
    uint32_t ReadNotMovePt(uint8_t* data, uint32_t len) override;
    uint32_t MoveReadPt(uint32_t len) override;
    uint32_t Read(uint8_t* data, uint32_t len) override;
    void VisitData(const std::function<bool(uint8_t*, uint32_t)>& visitor) override;
    // Query helpers.
    uint32_t GetDataLength() override;
    uint32_t GetDataLength() const;
    uint8_t* GetData() const;
    BufferSpan GetReadableSpan() const;
    void Clear() override;

    // Quick validity check for debugging and defensive programming.
    bool Valid() const;

private:
    // Shared implementation for Read()/ReadNotMovePt().
    uint32_t InnerRead(uint8_t* data, uint32_t len, bool move_pt);

    uint8_t* read_pos_ = nullptr;
    uint8_t* buffer_start_ = nullptr;
    uint8_t* buffer_end_ = nullptr;
};

}
}

#endif

