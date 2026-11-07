#ifndef HTTP3_QPACK_DYNAMIC_TABLE
#define HTTP3_QPACK_DYNAMIC_TABLE

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <utility>

#include "http3/qpack/type.h"
#include "http3/qpack/util.h"

namespace quicx {
namespace http3 {

class DynamicTable {
public:
    DynamicTable(uint32_t max_size);
    virtual ~DynamicTable();

    // Add a new header item to dynamic table
    // Returns the index of the added item, or -1 if failed
    bool AddHeaderItem(const std::string& name, const std::string& value);

    // Get header item by deque position (0 = newest). Used internally.
    HeaderItem* FindHeaderItem(uint32_t index);

    // Get header item by absolute index (RFC 9204 §3.2.4).
    // Absolute index = total_insert_count at insertion time - 1.
    // Converts to deque position internally.
    HeaderItem* FindHeaderItemByAbsoluteIndex(uint64_t absolute_index);

    // Get header item deque position by name and value.
    // Returns -1 if not found. NOTE: returns deque position, NOT absolute index.
    int32_t FindHeaderItemIndex(const std::string& name, const std::string& value);

    // Find absolute index (RFC 9204) by name and value.
    // Returns -1 if not found. Absolute index = total_insert_count_ - 1 - deque_position.
    int64_t FindAbsoluteIndex(const std::string& name, const std::string& value);

    // Get header item index by name only (first match), returns deque position or -1
    int32_t FindHeaderNameIndex(const std::string& name);

    // Find absolute index (RFC 9204) by name only (first match), returns -1 if not found
    int64_t FindAbsoluteNameIndex(const std::string& name);

    // P1-H4c (RFC 9204 §3.2.4 rule 2): track how many of OUR inserts the
    // peer has acknowledged (Insert Count Increment, encoder side).
    // WouldEvictUnacknowledged() is consulted ONLY by the local-encoder
    // insert path (qpack_encoder.cpp): inserting an entry that forces
    // eviction of an entry the peer has not yet acknowledged would desync
    // the tables — the encoder must fall back to a literal instead. The
    // decoder-side path (DecodeEncoderInstructions mirroring the peer's
    // inserts) is NOT guarded: its table follows the peer's Set Capacity
    // and the peer guarantees its own references.
    void AdvanceKnownReceived(uint64_t delta) { known_received_count_ += delta; }
    uint64_t GetKnownReceivedCount() const { return known_received_count_; }
    // Whether inserting an entry of |entry_size| would evict ANY entry the peer
    // has not acknowledged. Eviction is oldest-first and may take several
    // entries to make room, so checking only the oldest is not enough: the
    // k-th entry evicted can be unacknowledged even when the first is not.
    // Absolute insert numbers increase from oldest to newest, so it is enough to check
    // the LAST entry the eviction would remove: if that one is acknowledged,
    // every older one is too.
    //
    // "Protected" means any of:
    //   - not yet acknowledged by the peer (absolute index >= Known Received
    //     Count), RFC 9204 3.2.4 rule 2;
    //   - referenced by a header block the peer has not yet acknowledged or
    //     cancelled (absolute index >= the pin floor), RFC 9204 2.1.1: "an
    //     encoder MUST NOT evict an entry that has outstanding references".
    //     The peer may decode that block AFTER it has applied our next
    //     eviction (encoder stream and request stream are not ordered against
    //     each other), so an evicted-but-still-referenced entry means
    //     QPACK_DECOMPRESSION_FAILED -> the whole connection dies.
    bool WouldEvictProtectedEntry(uint32_t entry_size) const {
        if (headeritem_deque_.empty() || current_size_ + entry_size <= max_size_) {
            return false;  // no eviction needed
        }
        // Walk from the oldest (back) exactly as AddHeaderItem's eviction loop
        // does, until the new entry fits or the table is empty.
        uint64_t size = current_size_;
        size_t evicted = 0;
        for (auto it = headeritem_deque_.rbegin(); it != headeritem_deque_.rend(); ++it) {
            size -= static_cast<uint64_t>(it->name_.length() + it->value_.length() + 32);
            ++evicted;
            if (size + entry_size <= max_size_) {
                break;
            }
        }
        const uint64_t oldest_absolute = total_insert_count_ - headeritem_deque_.size();
        const uint64_t last_evicted_absolute = oldest_absolute + (evicted - 1);
        return last_evicted_absolute >= known_received_count_ || last_evicted_absolute >= pin_floor_;
    }

    // Lowest absolute index referenced by any outstanding header block; entries
    // at or above it must not be evicted (eviction is oldest-first, so evicting
    // anything >= floor would first evict the floor entry itself). UINT64_MAX =
    // nothing pinned. Maintained by QpackEncoder.
    void SetEvictionPinFloor(uint64_t floor) { pin_floor_ = floor; }
    uint64_t GetEvictionPinFloor() const { return pin_floor_; }

    // Evict entries to ensure table size doesn't exceed max_size_
    void EvictEntries();

    // Get current size of dynamic table
    uint32_t GetTableSize() const { return current_size_; }

    // Get maximum allowed size of dynamic table
    uint32_t GetMaxTableSize() const { return max_size_; }
    // Get current entry count in table
    uint32_t GetEntryCount() const { return static_cast<uint32_t>(headeritem_deque_.size()); }

    // Get total number of inserts (monotonically increasing, used for RIC encoding)
    uint64_t GetInsertCount() const { return total_insert_count_; }

    // Update maximum size of dynamic table
    void UpdateMaxTableSize(uint32_t new_size);

    // RFC 9204 Section 4.3.4: Duplicate an existing entry
    // Duplicates the entry at the given absolute index
    // Returns true if successful, false if index is invalid
    bool DuplicateEntry(uint32_t absolute_index);

private:
    // Calculate size of a header entry (per RFC 7541 Section 4.1)
    uint32_t CalculateEntrySize(const std::string& name, const std::string& value);

    std::deque<HeaderItem> headeritem_deque_;  // Dynamic table entries (front=newest, back=oldest)
    std::unordered_map<std::pair<std::string, std::string>, uint32_t, pair_hash> headeritem_index_map_;

    uint32_t max_size_;            // Maximum allowed size of dynamic table
    uint32_t current_size_;        // Current size of dynamic table
    uint64_t total_insert_count_;  // Total number of inserts (monotonically increasing)
    uint64_t known_received_count_{0};  // P1-H4c: inserts the peer has acknowledged (ICI / Section Ack)
    uint64_t pin_floor_{UINT64_MAX};    // see SetEvictionPinFloor()
};

}  // namespace http3
}  // namespace quicx

#endif  // HTTP3_QPACK_DYNAMIC_TABLE
