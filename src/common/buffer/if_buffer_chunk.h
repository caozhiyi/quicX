#ifndef COMMON_BUFFER_IF_BUFFER_CHUNK
#define COMMON_BUFFER_IF_BUFFER_CHUNK

#include <cstdint>
#include <memory>

namespace quicx {
namespace common {

class BlockMemoryPool;

class IBufferChunk {
public:
    virtual ~IBufferChunk() = default;

    /**
     * @brief Check if the buffer chunk is valid
     *
     * @return true if the buffer chunk contains valid memory
     * @return false if the buffer chunk is null or invalid
     */
    virtual bool Valid() const = 0;

    /**
     * @brief Get the raw data pointer of the buffer
     *
     * @return uint8_t* pointer to the start of the buffer data
     */
    virtual uint8_t* GetData() const = 0;

    /**
     * @brief Get the physical length of the underlying memory block.
     *
     * Zero-copy invariant 2 (capacity / logical length separation): this
     * value is a property of the chunk's storage and is immutable for the
     * lifetime of the chunk. It MUST NOT be used to convey "how many bytes
     * the current buffer view is allowed to use" — that policy belongs on
     * the owning IBuffer (see IBuffer::SetCapacityLimit, where applicable).
     *
     * @return uint32_t physical block size in bytes
     */
    virtual uint32_t GetLength() const = 0;

    /**
     * @brief Get the memory pool this buffer chunk belongs to
     *
     * @return std::shared_ptr<BlockMemoryPool> shared pointer to the memory pool
     */
    virtual std::shared_ptr<BlockMemoryPool> GetPool() const = 0;

    // -------------------------------------------------------------------------
    // Zero-copy invariant 1 (write-floor) hooks
    // -------------------------------------------------------------------------
    // The "write floor" is the lowest address inside this chunk that may be
    // written to. Any byte strictly below the floor has been issued out as
    // part of a SharedBufferSpan and MUST remain immutable for the lifetime
    // of every span that covers it.
    //
    // Default skeleton (returned by this base) is GetData(), i.e. "no bytes
    // are pinned"; the production implementation will track outstanding spans
    // via reference counts.
    //
    // FreezeUpTo / Unfreeze are called by SharedBufferSpan ctor/dtor.

    /**
     * @brief Pin the byte range [GetData(), end) so it cannot be overwritten
     *        until a matching Unfreeze() lands.
     *
     * The chunk keeps the highest pinned end-pointer as its current floor.
     * Calls with `end` below the existing floor are a no-op.
     */
    virtual void FreezeUpTo(uint8_t* /*end*/) {}

    /**
     * @brief Reverse a previous FreezeUpTo() call. After the last outstanding
     *        freeze is released the floor drops back to GetData().
     */
    virtual void Unfreeze(uint8_t* /*end*/) {}

    /**
     * @brief Lowest writable address inside this chunk. Writes that would
     *        land below the floor MUST be silently clamped or rejected by
     *        the owning IBuffer.
     *
     * Default base implementation reports GetData() (i.e. "no floor"). Each
     * concrete chunk can override this once it tracks freezes.
     */
    virtual uint8_t* GetWriteFloor() const { return GetData(); }
};

}  // namespace common
}  // namespace quicx

#endif
