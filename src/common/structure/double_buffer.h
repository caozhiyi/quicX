#ifndef COMMON_STRUCTURE_DOUBLE_BUFFER
#define COMMON_STRUCTURE_DOUBLE_BUFFER

#include <unordered_set>

namespace quicx {
namespace common {

/**
 * @brief Thread-safe double buffer for concurrent producer-consumer scenarios
 *
 * This pattern is used to safely manage active sets in event-driven systems where:
 * - Writers add items to the write buffer during processing
 * - Readers process items from the read buffer
 * - Buffers are swapped between iterations
 *
 * Example use cases:
 * - Worker's active connection set
 * - StreamManager's active stream set
 * - Any scenario where callbacks might add items during iteration
 *
 * Thread safety notes:
 * - Add() is safe to call during processing (writes to inactive buffer)
 * - Swap() should only be called from the processing thread
 * - GetReadBuffer() should only be used by the processing thread
 *
 * @tparam T Type of elements (must be hashable if using unordered_set)
 */
template <typename T>
class DoubleBuffer {
public:
    DoubleBuffer() : current_is_buffer1_(true) {}

    /**
     * @brief Add an item to the write buffer
     *
     * This is safe to call during read buffer processing because it writes
     * to the inactive buffer.
     *
     * @param item Item to add
     */
    void Add(const T& item) { GetWriteBuffer().insert(item); }

    /**
     * @brief Swap read and write buffers
     *
     * Merges unprocessed items from the old read buffer into the new read buffer
     * (which was the write buffer), then clears the old read buffer (which becomes
     * the new write buffer).
     *
     * Call this at the beginning of each processing iteration.
     */
    void Swap() {
        if (current_is_buffer1_) {
            // Current: Read=buffer1, Write=buffer2
            // Move unfinished items from Read(buffer1) to Write(buffer2)
            buffer2_.insert(buffer1_.begin(), buffer1_.end());
            buffer1_.clear();
            current_is_buffer1_ = false;
            // Now: Read=buffer2, Write=buffer1
        } else {
            // Current: Read=buffer2, Write=buffer1
            // Move unfinished items from Read(buffer2) to Write(buffer1)
            buffer1_.insert(buffer2_.begin(), buffer2_.end());
            buffer2_.clear();
            current_is_buffer1_ = true;
            // Now: Read=buffer1, Write=buffer2
        }
    }

    /**
     * @brief Get the read buffer (for processing)
     *
     * This buffer contains items to be processed in the current iteration.
     * Items can be removed from this buffer during processing.
     *
     * @return Reference to the read buffer
     */
    std::unordered_set<T>& GetReadBuffer() { return current_is_buffer1_ ? buffer1_ : buffer2_; }

    /**
     * @brief Get the read buffer (const version)
     */
    const std::unordered_set<T>& GetReadBuffer() const { return current_is_buffer1_ ? buffer1_ : buffer2_; }

    /**
     * @brief Get the write buffer (for adding items)
     *
     * This buffer receives new items during processing of the read buffer.
     *
     * @return Reference to the write buffer
     */
    std::unordered_set<T>& GetWriteBuffer() { return current_is_buffer1_ ? buffer2_ : buffer1_; }

    /**
     * @brief Check if both buffers are empty
     *
     * @return true if no items in either buffer
     */
    bool IsEmpty() const { return buffer1_.empty() && buffer2_.empty(); }

    /**
     * @brief Get total number of items in both buffers
     *
     * @return Total item count
     */
    size_t Size() const { return buffer1_.size() + buffer2_.size(); }

    /**
     * @brief Clear both buffers
     */
    void Clear() {
        buffer1_.clear();
        buffer2_.clear();
    }

private:
    bool current_is_buffer1_;
    std::unordered_set<T> buffer1_;
    std::unordered_set<T> buffer2_;
};

}  // namespace common
}  // namespace quicx

#endif  // COMMON_STRUCTURE_DOUBLE_BUFFER
